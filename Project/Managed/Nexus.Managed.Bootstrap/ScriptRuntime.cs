using System.Reflection;
using System.Text;
using Nexus.Managed;
using Nexus.Managed.Interop;

namespace Nexus.Managed.Bootstrap;
/*-----------------------------------------------------------------------------------------
 * ScriptRuntime
 * - Native ScriptComponentのsnapshotに対応するScriptBehaviourだけを所有する
 * - Entity / Component Storageは持たず、生存・有効状態は毎回Native APIで確認する
 *---------------------------------------------------------------------------------------*/
internal sealed unsafe class ScriptRuntime
{
    private readonly GameWorld world;
    private readonly ScriptMetadata metadata; //< 世代所有。Unload前にRuntimeごと強参照を解除する
    private readonly Dictionary<ulong, Instance> instances = new(); //< Managed object所有用。ECSではない
    private ulong next = 1;
    private bool stopping;
    private bool cleanupFailed; //< callback解除の失敗はUnload完了だけでは回復しない。同じ世代では成功へ戻さない
    /*-----------------------------------------------------------------------------------------
     * Instance
     * - 一つの関連付けのlifecycle状態とManaged objectを所有する
     * - Ownerは数値Handle。Native callback bufferやComponentアドレスを保持しない
     *---------------------------------------------------------------------------------------*/
    private sealed class Instance
    {
        internal ulong Id, Attachment;
        internal EntityHandle Owner;
        internal string Type = "";
        internal ScriptBehaviour? Behaviour;
        internal ScriptTypeMetadata? Metadata;
        internal bool Created, Active, Faulted, Removed;
    }
    internal ulong NextId => next; //< 同一sessionのReload後にも数値IDを再利用しない
    internal ScriptRuntime(GameWorld owner, IEnumerable<Assembly> assemblies, ulong firstId = 1) { world = owner; metadata = new ScriptMetadata(assemblies); next = firstId; }
    internal static string Text(NativeString value)
    {
        if (value.Length > 131072 || (value.Length != 0 && value.Data == null)) throw new ArgumentException("Invalid UTF-8 buffer.");
        return new UTF8Encoding(false, true).GetString(value.Data, checked((int)value.Length));
    }
    private bool Current(Instance item, out ScriptState state) => world.TryScript(item.Owner, item.Attachment, out state);
    private void Report(Instance item, Exception exception)
    {
        try { world.Debug.LogError($"Script {item.Type}: {exception}"); }
        catch {
            // 旧Hostの追加サービス未提供時も、従来の必須Loggerで例外を失わない。
            try {
                var message = $"Script {item.Type}: {exception}";
                world.Log(message.Length > 2048 ? message.Substring(0, 2048) : message);
            }
            catch { /* 診断失敗を二次例外にしない。 */ }
        }
    }
    /// <summary>後続cleanupを継続しながら失敗を記録する。例外objectで旧Assemblyをrootさせない。</summary>
    private void CleanupFailure(Instance item, Exception exception) { cleanupFailed = true; Report(item, exception); }
    private void Fault(Instance item, Exception exception)
    {
        if (!item.Faulted) Report(item, exception);
        item.Faulted = true;
        if (item.Active)
        {
            item.Active = false;
            try { item.Behaviour?.Disable(); } catch (Exception cleanup) { CleanupFailure(item, cleanup); }
        }
    }
    private void Invoke(Instance item, Action action) { try { action(); } catch (Exception exception) { Fault(item, exception); } }
    private void Release(Instance item)
    {
        if (item.Removed) return;
        item.Removed = true; instances.Remove(item.Id);
        try { item.Behaviour?.BeginStop(); } catch (Exception exception) { CleanupFailure(item, exception); }
        if (item.Active) { item.Active = false; try { item.Behaviour?.Disable(); } catch (Exception exception) { CleanupFailure(item, exception); } }
        try { if (item.Created) item.Behaviour?.Destroy(); } catch (Exception exception) { CleanupFailure(item, exception); }
        finally {
            try { item.Behaviour?.Unbind(); } catch (Exception exception) { CleanupFailure(item, exception); }
            finally { item.Behaviour = null; item.Metadata = null; }
        }
    }
    private void Reconcile(Instance item)
    {
        if (!Current(item, out var state)) { Release(item); return; }
        var enabled = state.Enabled != 0 && !item.Faulted;
        if (item.Active && !enabled) {
            item.Active = false;
            try { item.Behaviour!.Disable(); }
            catch (Exception exception) { cleanupFailed = true; Fault(item, exception); }
        }
        else if (!item.Active && enabled) { item.Active = true; Invoke(item, () => item.Behaviour!.Enable()); }
    }
    /// <summary>Nativeの反復終了後に一括同期する。callbackによる自己破棄・型置換もHandleで再検証する。</summary>
    internal bool Tick(Span<ScriptBinding> bindings, float deltaTime, bool initializeOnly, bool fixedOnly = false)
    {
        if (stopping) throw new InvalidOperationException("Script runtime is stopping.");
        var names = new string[bindings.Length]; var keep = new HashSet<ulong>();
        for (var index = 0; index < bindings.Length; ++index)
        {
            ref var binding = ref bindings[index]; names[index] = Text(binding.Type);
            // Typeを先に検証し、生成の副作用前に不明な型を拒否する。同型instanceの寿命はattachmentで区別する。
            _ = metadata.Resolve(names[index]); // 同じ型でもattachmentごとに独立したinstanceとして所有する。
            if (instances.TryGetValue(binding.Instance, out var old) && old.Owner.Equals(binding.Entity) &&
                old.Attachment == binding.Attachment && old.Type == names[index]) keep.Add(old.Id);
        }
        foreach (var old in instances.Values.ToArray()) if (!keep.Contains(old.Id)) Release(old);
        // 解除に失敗した世代へ新規Behaviourや通常Updateを追加しない。残りの解放はStopへ委ねる。
        if (cleanupFailed) return false;
        var initialFailure = false;
        for (var index = 0; index < bindings.Length; ++index)
        {
            if (cleanupFailed) break;
            ref var binding = ref bindings[index]; var previousId = binding.Instance; binding.Instance = 0;
            if (!world.TryScript(binding.Entity, binding.Attachment, out var state)) continue;
            if (!instances.TryGetValue(previousId, out var item) || !keep.Contains(previousId) || !item.Owner.Equals(binding.Entity) ||
                item.Attachment != binding.Attachment || item.Type != names[index])
            {
                if (next == 0) throw new InvalidOperationException("Script instance ID space exhausted.");
                item = new Instance { Id = next, Owner = binding.Entity, Attachment = binding.Attachment, Type = names[index] };
                next = unchecked(next + 1); instances.Add(item.Id, item);
                try
                {
                    item.Metadata = metadata.Resolve(item.Type);
                    var type = item.Metadata.Type;
                    if (type.IsAbstract || !typeof(ScriptBehaviour).IsAssignableFrom(type) || type.GetConstructor(Type.EmptyTypes) == null)
                        throw new InvalidOperationException("Script type must be a concrete ScriptBehaviour with public parameterless constructor.");
                    item.Behaviour = (ScriptBehaviour)Activator.CreateInstance(type)!;
                    // 保存値はOnCreateより前に復元する。setterからWorldを操作できないようBindにも先行する。
                    if (world.HasPropertyApi) item.Metadata.Restore(item.Behaviour, world.ReadProperties(item.Owner, item.Attachment));
                    item.Behaviour.Bind(world, item.Owner, item.Attachment); item.Created = true;
                    Invoke(item, item.Behaviour.Create);
                }
                catch (Exception exception) { Fault(item, exception); }
            }
            Reconcile(item);
            if (!cleanupFailed && !initializeOnly && !item.Removed && item.Active && Current(item, out var active) && active.Enabled != 0)
                Invoke(item, () => { if(fixedOnly) item.Behaviour!.FixedUpdate(deltaTime); else item.Behaviour!.Update(deltaTime); });
            if (!item.Removed) Reconcile(item);
            if (!item.Removed) binding.Instance = item.Id;
            if (initializeOnly && item.Faulted) initialFailure = true;
        }
        if(!initializeOnly && !fixedOnly) world.TickTimers();
        // 全behaviourの更新が終わってから保存する。後続behaviourによる値変更も同じsnapshotへ含める。
        foreach (var item in instances.Values.ToArray())
        {
            if (cleanupFailed) break;
            if (!item.Removed && !item.Faulted && world.HasPropertyApi && item.Metadata!.Properties.Count != 0 && Current(item, out _))
                Invoke(item, () => {
                    // Property単位のInteropを避け、全公開値を一つの状態としてcopyする。
                    var previous = world.ReadProperties(item.Owner, item.Attachment);
                    var snapshot = item.Metadata.Capture(item.Behaviour!, previous);
                    if (snapshot != previous) world.WriteProperties(item.Owner, item.Attachment, snapshot);
                });
            if (initializeOnly && item.Faulted) initialFailure = true;
        }
        // 後続callbackが、すでに更新したEntityを破棄した場合も同じframe末で参照を解除する。
        foreach (var item in instances.Values.ToArray()) if (!Current(item, out _)) Release(item);
        return !initialFailure && !cleanupFailed;
    }
    /// <summary>Worldが生存する間に全behaviourのOnDisable / OnDestroyと参照解除を試行する。</summary>
    /// <returns>以前の解除失敗も含めて全cleanupが成功した場合だけtrue。GC解放成功とは区別する。</returns>
    internal bool Stop()
    {
        if (stopping) return !cleanupFailed;
        stopping = true;
        try { foreach (var item in instances.Values.ToArray()) Release(item); }
        catch { cleanupFailed = true; throw; }
        finally { instances.Clear(); }
        return !cleanupFailed;
    }
    /// <summary>停止より前に状態を確定する。snapshotは数値Handleと文字列のみで旧Assemblyをrootしない。</summary>
    internal List<ScriptSnapshot> CaptureState()
    {
        if (cleanupFailed) throw new InvalidOperationException("Script cleanup failed; restart the host before reloading.");
        var result = new List<ScriptSnapshot>();
        foreach (var item in instances.Values.ToArray())
        {
            if (!Current(item, out _)) continue;
            var text = world.ReadProperties(item.Owner, item.Attachment);
            if (!item.Faulted && item.Behaviour != null && item.Metadata != null) text = item.Metadata.Capture(item.Behaviour, text);
            if (!Current(item, out _)) continue;
            world.WriteProperties(item.Owner, item.Attachment, text);
            result.Add(new ScriptSnapshot(item.Type, text));
        }
        return result;
    }
}
/* 値だけのReload snapshot。PropertyInfo / Type / behaviourは含めない。 */
internal sealed record ScriptSnapshot(string Type, string State);
