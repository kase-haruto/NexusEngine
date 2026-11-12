using System.Runtime.InteropServices;
using Nexus.Managed.Interop;

namespace Nexus.Managed;
/*-----------------------------------------------------------------------------------------
 * ScriptAttachmentRequest
 * - ScriptAttachments.hの24-byte固定header。後続型IDだけをUTF-8値としてコピーする
 *---------------------------------------------------------------------------------------*/
[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal struct ScriptAttachmentRequest
{
    internal uint Operation, Enabled, TypeLength, Index;
    internal ulong Attachment;
}
/*-----------------------------------------------------------------------------------------
 * GameScript
 * - Entityと関連付けtokenから現在の一つのScriptを解決する非所有wrapper
 * - Managed object/GCHandle/Native pointerを公開せず、解除後の旧token操作を拒否する
 *---------------------------------------------------------------------------------------*/
public sealed class GameScript
{
    private readonly Entity entity; //< session所有GameWorldを借用する
    private readonly ulong attachment; //< 削除・再追加後に再利用しない関連付けtoken
    public string TypeId { get; }
    internal GameScript(Entity owner, ulong token, string type) { entity = owner; attachment = token; TypeId = type; }
    public Entity Entity => entity;
    public bool IsAttached => entity.World.TryScript(entity.Handle, attachment, out _);
    public bool Enabled {
        get => entity.World.TryScript(entity.Handle, attachment, out var state) ? state.Enabled != 0 : throw new InvalidOperationException("Script attachment is no longer valid.");
        set => entity.World.EnableScript(entity.Handle, attachment, value);
    }
    /// <summary>このScriptだけを解除する。同じEntityの別Scriptは保持する。</summary>
    public void Remove() => entity.World.ScriptCall(4, entity.Handle, attachment);
}
/*-----------------------------------------------------------------------------------------
 * GameWorld (scripts)
 * - 既存services/thread権限でScript関連付けを操作する。Managed実体の所有はBootstrapに限定する
 *---------------------------------------------------------------------------------------*/
public sealed unsafe partial class GameWorld
{
    private static readonly byte[] ScriptCommand = System.Text.Encoding.UTF8.GetBytes("nexus.engine.scripts"); //< partial classの初期化順へ依存せず固定IDを生成する
    /// <summary>旧Bootstrap経路では主Scriptだけを検証する。新経路は数値tokenで全関連付けを解決する。</summary>
    internal bool TryScript(EntityHandle handle, ulong attachment, out ScriptState state)
    {
        state = default; if(token == 0) return false; Access(handle);
        if(servicesClient == 0) return TryScript(handle, out state) && state.Attachment == attachment;
        // Lifecycle中の高頻度検査は固定stack値だけを渡す。PropertyごとのInteropや4 KiB配列確保を増やさない。
        var request = new ScriptAttachmentRequest { Operation = 2, Attachment = attachment }; ScriptState copy = default; uint length = 0; int status;
        fixed(byte* name = ScriptCommand) status = services.Invoke(servicesClient, new NativeString { Data = name, Length = (uint)ScriptCommand.Length }, 1, handle,
            new NativeBytes { Data = (byte*)&request, Length = 24 }, (byte*)&copy, 16, &length);
        if(status == 2) return false; Check(status);
        if(length != 16) throw new InvalidOperationException("Invalid Script state response."); state = copy;
        if(state.Reserved != 0 || state.Enabled > 1 || (state.Attachment != 0 && state.Attachment != attachment)) throw new InvalidOperationException("Invalid Script state.");
        return state.Attachment != 0;
    }
    internal void EnableScript(EntityHandle handle, ulong attachment, bool enabled) {
        if(servicesClient == 0) {
            if(!TryScript(handle, attachment, out _)) throw new InvalidOperationException("Script attachment is no longer valid.");
            EnableScript(handle, enabled); return;
        }
        _ = ScriptCall(3, handle, attachment, enabled);
    }
    /// <summary>共通client/thread/旧世代書込制約を使う。mutationを容量照会で二度呼ばない。</summary>
    internal byte[] ScriptCall(uint operation, EntityHandle entity, ulong attachment = 0, bool enabled = false, string type = "", uint index = 0)
    {
        if(sizeof(ScriptAttachmentRequest) != 24 || sizeof(ScriptState) != 16) throw new InvalidOperationException("Script ABI layout mismatch.");
        var text = ComponentEncoding.GetBytes(type);
        if(text.Length > 512) throw new ArgumentOutOfRangeException(nameof(type));
        var request = new byte[sizeof(ScriptAttachmentRequest) + text.Length];
        var header = new ScriptAttachmentRequest { Operation = operation, Enabled = enabled ? 1u : 0u, TypeLength = (uint)text.Length, Index = index, Attachment = attachment };
        fixed(byte* data = request) *(ScriptAttachmentRequest*)data = header;
        Buffer.BlockCopy(text, 0, request, sizeof(ScriptAttachmentRequest), text.Length);
        var result = InvokeWorldCommand("nexus.engine.scripts", entity, request, operation is 1 or 3 or 4 or 6);
        if((operation is 3 or 4) && result.Length != 0) throw new InvalidOperationException("Invalid Script mutation response.");
        return result;
    }
    internal GameScript AddScript(Entity entity, Type behaviourType, bool enabled, bool independent=false)
    {
        Access(entity.Handle); WriteAccess();
        if(behaviourType.IsAbstract || behaviourType.ContainsGenericParameters || behaviourType.GetConstructor(Type.EmptyTypes) == null)
            throw new ArgumentException("Script requires a concrete type with a public parameterless constructor.", nameof(behaviourType));
        var metadata = Attribute.GetCustomAttribute(behaviourType, typeof(ScriptTypeAttribute)) as ScriptTypeAttribute;
        var type = metadata?.Id ?? behaviourType.FullName!;
        // rename前のID/FullNameも同じ型として扱う。追加操作で二重関連付けを作ってから失敗させない。
        if(!independent) foreach(var existing in Scripts(entity))
            if(existing.TypeId == type || existing.TypeId == behaviourType.FullName || (metadata?.Aliases.Contains(existing.TypeId) ?? false)) return existing;
        var output = ScriptCall(independent ? 6u : 1u, entity.Handle, enabled: enabled, type: type);
        if(output.Length != sizeof(ScriptState)) throw new InvalidOperationException("Invalid Script add response.");
        ScriptState state; fixed(byte* data = output) state = *(ScriptState*)data;
        if(state.Attachment == 0 || state.Reserved != 0 || state.Enabled > 1) throw new InvalidOperationException("Invalid Script attachment.");
        return new GameScript(entity, state.Attachment, type);
    }
    internal GameScript[] Scripts(Entity entity)
    {
        var result = new List<GameScript>();
        for(uint index = 0; index <= 32; ++index) {
            var output = ScriptCall(5, entity.Handle, index: index);
            if(output.Length < sizeof(ScriptState) || output.Length > sizeof(ScriptState) + 512) throw new InvalidOperationException("Invalid Script list response.");
            ScriptState state; fixed(byte* data = output) state = *(ScriptState*)data;
            if(state.Reserved != 0 || state.Enabled > 1) throw new InvalidOperationException("Invalid Script state.");
            if(state.Attachment == 0) { if(output.Length != sizeof(ScriptState)) throw new InvalidOperationException("Invalid Script list end."); return result.ToArray(); }
            var type = ComponentEncoding.GetString(output, sizeof(ScriptState), output.Length - sizeof(ScriptState));
            if(type.Length == 0) throw new InvalidOperationException("Missing Script Type ID.");
            result.Add(new GameScript(entity, state.Attachment, type));
        }
        throw new InvalidOperationException("Script count exceeds 32.");
    }
}
