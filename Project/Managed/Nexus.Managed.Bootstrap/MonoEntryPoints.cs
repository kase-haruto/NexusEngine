using System.Runtime.InteropServices;
using System.Text.Json;
using Nexus.Managed.Interop;

namespace Nexus.Managed.Bootstrap;
/*-----------------------------------------------------------------------------------------
 * EntryPoints (Mono)
 * - 既存C ABIへCdecl delegate thunkを接続し、Domainの存続中だけ強参照を保持する
 * - Domainの生成・unloadとその成否確認はNative MonoHostへ委譲する
 *---------------------------------------------------------------------------------------*/
public static unsafe partial class EntryPoints
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int BootstrapStart(BootstrapContext* context, ulong* session);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int GameplayStart(GameplayContext* context, ulong* session);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int ReflectedStart(ReflectedGameplayContext* context, ulong* session);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int HybridStart(HybridGameplayContext* context, ulong* session);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int BoundStart(BoundGameplayContext* context, ulong* session);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int StopSession(ulong session);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int ScriptAttachmentFeature(uint abi);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int TickSession(ulong session, ScriptBinding* bindings, uint count, float deltaTime, uint initializeOnly);
    private static ulong monoInstanceSeed = 1; //< Domain差替え後もinstance IDを再利用しない
    private static readonly Dictionary<string, Delegate> NativeEntries = new(StringComparer.Ordinal) {
        ["Start"] = new BootstrapStart(Start), ["StartGameplay"] = new GameplayStart(StartGameplay),
        ["StartReflectedGameplay"] = new ReflectedStart(StartReflectedGameplay), ["StartHybridGameplay"] = new HybridStart(StartHybridGameplay),
        ["StartBoundGameplay"] = new BoundStart(StartBoundGameplay), ["Stop"] = new StopSession(Stop), ["Tick"] = new TickSession(Tick),
        ["SupportsFixedUpdates"] = new ScriptAttachmentFeature(SupportsFixedUpdates),
        ["SupportsScriptAttachments"] = new ScriptAttachmentFeature(SupportsScriptAttachments)
    }; //< Native側はunload前に全pointerを破棄する。このDomain内だけでdelegateをrootする

    /// <summary>Mono embeddingのruntime_invokeからだけ呼ぶ。関数表をGame Assemblyへ公開しない。</summary>
    public static IntPtr ResolveNativeEntry(string name) => Marshal.GetFunctionPointerForDelegate(NativeEntries[name]);
    /// <summary>Nativeが所有するDomain専用directoryを開始前に設定する。</summary>
    public static void ConfigureDomain(string directory) => GameScriptCatalog.ShadowRoot = Path.GetFullPath(directory);
    /// <summary>次のDomainへ値だけのinstance counterを引き継ぐ。</summary>
    public static void SetDomainInstanceSeed(ulong seed) => monoInstanceSeed = seed != 0 ? seed : throw new InvalidOperationException("Script instance counter exhausted.");

    private static void RetireGame(Session session)
    {
        try {
            if(session.Gameplay != null && !session.Gameplay.Stop()) session.CleanupFailed = true;
        }
        catch { session.CleanupFailed = true; throw; }
        finally {
            session.Gameplay = null;
            try { session.World?.Invalidate(); }
            finally { session.World = null; session.Catalog?.Dispose(); session.Catalog = null; }
        }
    }
    // この戻り値はlifecycle cleanupの成否だけ。Domain解放はNative MonoHostが別に確認する。
    private static bool ConfirmRetired(Session session) => !session.CleanupFailed;

    /// <summary>既存Native Componentへ公開値を保存し、Domain境界を越えるJSONだけを返す。</summary>
    public static string CaptureForDomainReload(ulong id)
    {
        var session = SessionRegistry.Sessions[id];
        if(session.Thread != Environment.CurrentManagedThreadId || session.Busy || session.CleanupFailed || session.Gameplay == null)
            throw new InvalidOperationException("Mono reload requires idle gameplay on its owner thread.");
        session.Busy = true;
        try {
            session.World?.DisallowWrites();
            var snapshot = session.Gameplay.CaptureState();
            return JsonSerializer.Serialize(new DomainSnapshot(session.Gameplay.NextId, snapshot));
        }
        finally { session.World?.AllowWrites(); session.Busy = false; }
    }
    /// <summary>隔離した候補Domainでconstructorと保存値を検証し、immutable shadowのpathを返す。</summary>
    public static string PreflightDomainReload(string assembly, string snapshot)
    {
        using var catalog = new GameScriptCatalog(assembly);
        var metadata = new ScriptMetadata(catalog.Assemblies);
        var state = JsonSerializer.Deserialize<DomainSnapshot>(snapshot) ?? throw new InvalidOperationException("Invalid reload snapshot.");
        foreach(var item in state.Scripts) {
            var type = metadata.Resolve(item.Type);
            var behaviour = (ScriptBehaviour)Activator.CreateInstance(type.Type)!;
            try { type.Restore(behaviour, item.State); }
            finally { try { behaviour.BeginStop(); } finally { behaviour.Unbind(); } }
        }
        return catalog.File;
    }
    /// <summary>候補検証後、旧世代の終了callbackによるWorld書込を停止する。</summary>
    public static void BeginDomainRetirement(ulong id) => SessionRegistry.Sessions[id].World?.DisallowWrites();
    /// <summary>初期化中は通常Tickを止め、共通Runtimeの状態復元後に更新を再開する。</summary>
    public static void SetDomainActivation(ulong id, bool pending) => SessionRegistry.Sessions[id].ReloadCommitting = pending;
    /// <summary>Native hostへcounterだけを返す。Game objectをDomain外へ渡さない。</summary>
    public static ulong DomainNextInstance(ulong id) => SessionRegistry.Sessions[id].Gameplay?.NextId ?? monoInstanceSeed;
    private sealed record DomainSnapshot(ulong NextInstance, List<ScriptSnapshot> Scripts);
}
