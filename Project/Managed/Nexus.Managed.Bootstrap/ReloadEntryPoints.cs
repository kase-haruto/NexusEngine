using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Nexus.Managed;
using Nexus.Managed.Interop;

namespace Nexus.Managed.Bootstrap;
public static unsafe partial class EntryPoints
{
    /// <summary>Runtime / object / metadata / World capabilityを全て解除してからUnloadを要求する。</summary>
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void RetireGame(Session session)
    {
        try {
            if (session.Gameplay != null) {
                session.NextInstance = session.Gameplay.NextId;
                // GCの参照消失と外部resourceの解除成功は別の条件。失敗後も全Behaviourの解放を続ける。
                if (!session.Gameplay.Stop()) { session.CleanupFailed = true; session.Write("Script cleanup failed; restart the host before resuming gameplay."); }
            }
        }
        catch { session.CleanupFailed = true; throw; }
        finally {
            session.Gameplay = null;
            try { session.World?.Invalidate(); }
            catch { session.CleanupFailed = true; throw; }
            finally {
                session.World = null;
                if (session.Catalog != null) {
                    try { session.Retired.Add(session.Catalog.Release()); session.Catalog = null; }
                    catch { session.CleanupFailed = true; throw; }
                }
            }
        }
    }
    /// <summary>helperのstackが戻ってからGCで確認する。未解放contextがあれば新世代へ進まない。</summary>
    private static bool ConfirmRetired(Session session)
    {
        // 解除失敗を単なる生存参照と区別し、後でGCが完了しても成功へ戻さない。
        var clean = !session.CleanupFailed;
        foreach (var retired in session.Retired.ToArray())
        {
            // ALCのUnloading callback失敗も外部参照解除の失敗であり、単なるGC待ちとして再開しない。
            if (retired.UnloadError != null) session.CleanupFailed = true;
            if (!retired.Confirm()) { if(retired.UnloadError != null) session.Write(retired.UnloadError); clean = false; continue; }
            if (Path.GetDirectoryName(session.PendingAssembly) == retired.DirectoryPath) continue;
            try { GameScriptCatalog.DeleteShadow(retired.DirectoryPath); session.Retired.Remove(retired); }
            catch (Exception exception) when (exception is IOException or UnauthorizedAccessException) { /* ファイル掃除の失敗をCLR解放失敗と混同しない。次回に再試行する。 */ }
        }
        return clean;
    }
    /// <summary>旧sessionを変更せず新型・constructor・保存値を検証する。例外自体は外へ保持しない。</summary>
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static string? Preflight(Session session, string file, List<ScriptSnapshot> snapshot)
    {
        GameScriptCatalog? candidate = null;
        try {
            candidate = new GameScriptCatalog(file);
            var metadata = new ScriptMetadata(candidate.Assemblies);
            foreach (var item in snapshot) {
                var type = metadata.Resolve(item.Type);
                var behaviour = (ScriptBehaviour)Activator.CreateInstance(type.Type)!;
                try { type.Restore(behaviour, item.State); }
                finally {
                    try { try { behaviour.BeginStop(); } finally { behaviour.Unbind(); } }
                    catch { session.CleanupFailed = true; throw; }
                }
            }
            session.PendingAssembly = candidate.File;
            return null;
        }
        catch (Exception exception) { return $"Reload preflight: {exception.GetType().Name}: {exception.Message}"; }
        finally { if (candidate != null) session.Retired.Add(candidate.Release()); }
    }
    /// <summary>Game Assembly参照をこのstack内へ閉じ込め、失敗時のUnload確認へ持ち越さない。</summary>
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static string? Activate(Session session)
    {
        try {
            session.Catalog = new GameScriptCatalog(session.PendingAssembly);
            session.World = session.CreateWorld();
            session.Gameplay = new ScriptRuntime(session.World, session.Catalog.Assemblies, session.NextInstance);
            session.PendingAssembly = ""; session.Suspended = false; return null;
        }
        catch (Exception exception) { return $"Reload activation: {exception.GetType().Name}: {exception.Message}"; }
    }
    /// <summary>0=Prepare、1=Commit、2=Abort、3=Complete。Native frame境界からだけ呼ぶ。</summary>
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int Reload(ulong sessionId, uint operation, NativeString file, uint* suspended)
    {
        if (suspended == null || operation > 3) return InvalidArgument; *suspended = 0;
        Session? session = null;
        try {
            lock (SessionRegistry.Gate) {
                if (!SessionRegistry.Sessions.TryGetValue(sessionId, out session)) return InvalidHandle;
                if (session.Thread != Environment.CurrentManagedThreadId) return WrongThread;
                *suspended = session.Suspended ? 1u : 0u;
                if (session.Busy || session.PropertyApi.Read == null) return Failed;
                session.Busy = true;
            }
            try {
                if (operation == 0) {
                    if (session.ReloadCommitting && !session.Suspended) return Failed; // Complete前の候補を重ねて入れ替えない。
                    session.PendingAssembly = "";
                    if (!ConfirmRetired(session)) { session.Write(session.CleanupFailed ? "Reload blocked: cleanup failed; restart the host." : "Reload blocked: context still rooted by static / event / delegate / GCHandle / Thread / Task / callback."); return Failed; }
                    session.ReloadCommitting = false; // 停止済み世代からの再試行も、新しいPrepareとして扱う。
                    string? error;
                    // Snapshot用getterからの副作用で、Preflight失敗時に実行Worldを変更させない。
                    try {
                        session.World?.DisallowWrites();
                        var snapshot = session.Gameplay?.CaptureState() ?? [];
                        error = Preflight(session, ScriptRuntime.Text(file), snapshot);
                    }
                    finally { session.World?.AllowWrites(); }
                    if (!ConfirmRetired(session) || error != null) {
                        session.PendingAssembly = ""; session.Write(error ?? "Reload preflight context did not unload."); return Failed;
                    }
                    return Ok;
                }
                if (operation == 1) {
                    if (session.PendingAssembly.Length == 0 || session.ReloadCommitting) return InvalidArgument;
                    session.ReloadCommitting = true;
                    session.Suspended = true; *suspended = 1;
                    session.World?.DisallowWrites();
                    RetireGame(session);
                    if (!ConfirmRetired(session)) { session.Write(session.CleanupFailed ? "Old GameScripts cleanup failed; World retained and scripts suspended. Restart the host." : "Old GameScripts did not unload; World retained and scripts suspended."); return Failed; }
                    var error = Activate(session);
                    if (error != null) { RetireGame(session); session.Write(error); return Failed; }
                    *suspended = 0; return Ok;
                }
                if (operation == 2) {
                    if (!session.ReloadCommitting) {
                        // Clone / 接続失敗等のPrepare取消では、稼働中のWorldとBehaviourを維持する。
                        session.PendingAssembly = "";
                        return ConfirmRetired(session) ? Ok : Failed;
                    }
                    session.Suspended = true; *suspended = 1; session.World?.DisallowWrites(); RetireGame(session); session.PendingAssembly = "";
                    return ConfirmRetired(session) ? Ok : Failed;
                }
                if (!session.ReloadCommitting || session.Suspended || session.Gameplay == null) return Failed;
                if (!ConfirmRetired(session)) return Failed;
                session.ReloadCommitting = false; return Ok;
            }
            finally {
                // 候補のcleanup失敗も外部参照の安全を保証できないため、旧世代へ通常実行を戻さない。
                if (session.CleanupFailed) { session.Suspended = true; session.World?.DisallowWrites(); }
                session.Busy = false; *suspended = session.Suspended ? 1u : 0u;
            }
        }
        catch (Exception exception) { Report(session, exception); if (session != null) *suspended = session.Suspended ? 1u : 0u; return Failed; }
    }
}
