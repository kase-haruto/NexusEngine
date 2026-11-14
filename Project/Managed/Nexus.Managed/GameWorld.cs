using System.Text;
using Nexus.Managed.Interop;

namespace Nexus.Managed;
/*-----------------------------------------------------------------------------------------
 * GameWorld
 * - 既存Native Worldへの権限と関数表を保持する。ECSやEntityの別Storageは作らない
 * - 所有threadとセッション寿命を毎回検証し、static等に残ったwrapperもStopで無効化する
 *---------------------------------------------------------------------------------------*/
public sealed unsafe partial class GameWorld
{
    private NativeGameApi api;
    private NativeScriptApi scripts;
    private NativeScriptPropertyApi properties; //< 永続状態の値copy専用。Componentアドレスは保持しない
    private ulong token;
    private bool writesBlocked; //< Reloadの旧世代cleanupから、保持するNative Worldを変更させない
    private readonly int thread = Environment.CurrentManagedThreadId;
    internal GameWorld(ulong world, NativeGameApi gameApi, NativeScriptApi scriptApi, NativeScriptPropertyApi propertyApi = default, NativeGameServicesApi serviceApi = default, NativeBindingApi bindingApi = default) {
        token = world; api = gameApi; scripts = scriptApi; properties = propertyApi; services = serviceApi; nativeBindings = bindingApi;
        if(services.Open != null) { ulong client = 0; Check(services.Open(world, &client)); servicesClient = client; }
    }
    private void Access()
    {
        if (token == 0) throw new ObjectDisposedException(nameof(GameWorld));
        if (thread != Environment.CurrentManagedThreadId) throw new InvalidOperationException("Game API requires the session thread.");
    }
    private void Access(EntityHandle handle) { Access(); if(handle.World != token) throw new InvalidOperationException("Entity belongs to another World."); }
    internal void DisallowWrites() { Access(); writesBlocked = true; }
    internal void AllowWrites() { Access(); writesBlocked = false; }
    private void WriteAccess() { Access(); if(writesBlocked) throw new InvalidOperationException("World mutation is unavailable during reload cleanup."); }
    internal void Invalidate() {
        try { if(servicesClient != 0) Check(services.Close(servicesClient)); }
        finally { ClearTimers(); servicesClient = 0; services = default; nativeBindings = default; token = 0; api = default; scripts = default; properties = default; }
    }
    internal bool HasPropertyApi => properties.Read != null;
    /// <summary>所有threadでcaller-owned bufferへコピーする。旧関連付けへの書込はNativeが拒否する。</summary>
    internal string ReadProperties(EntityHandle handle, ulong attachment)
    {
        Access(handle); uint count = 0;
        var status = properties.Read(handle, attachment, null, 0, &count);
        if (status != 0 && status != 4) Check(status);
        if (count > 262144) throw new InvalidOperationException("Script state exceeds 256 KiB.");
        var bytes = new byte[checked((int)count)];
        fixed (byte* buffer = bytes) Check(properties.Read(handle, attachment, buffer, count, &count));
        return new UTF8Encoding(false, true).GetString(bytes);
    }
    internal void WriteProperties(EntityHandle handle, ulong attachment, string text)
    {
        Access(handle); var bytes = Encoding.UTF8.GetBytes(text);
        fixed (byte* buffer = bytes) Check(properties.Write(handle, attachment, new NativeString { Data = buffer, Length = checked((uint)bytes.Length) }));
    }
    internal Entity Wrap(EntityHandle handle) { Access(handle); return new(this, handle); }
    internal static void Check(int status)
    {
        if (status != 0) throw new InvalidOperationException($"Nexus Game API returned status {status}.");
    }
    /// <summary>既存Levelに永続IDとTransformを持つEntityを生成する。</summary>
    public Entity CreateEntity(string name = "Entity")
    {
        WriteAccess(); if(name == null) throw new ArgumentNullException(nameof(name));
        var bytes = Encoding.UTF8.GetBytes(name); EntityHandle handle = default;
        fixed (byte* data = bytes) Check(api.Create(token, new NativeString { Data = data, Length = checked((uint)bytes.Length) }, &handle));
        return Wrap(handle);
    }
    /// <summary>世代付きHandleを一括取得する。Native objectのアドレスを返さない。</summary>
    public Entity[] Entities()
    {
        Access(); uint count = 0;
        var status = api.Entities(token, null, 0, &count);
        if (status != 0 && status != 4) Check(status);
        var handles = new EntityHandle[checked((int)count)];
        fixed (EntityHandle* data = handles) Check(api.Entities(token, data, count, &count));
        return handles.Select(Wrap).ToArray();
    }
    public void Log(string message)
    {
        Access(); if(message == null) throw new ArgumentNullException(nameof(message));
        var bytes = Encoding.UTF8.GetBytes(message);
        fixed (byte* data = bytes) Check(api.Log(token, new NativeString { Data = data, Length = checked((uint)bytes.Length) }));
    }
    internal bool IsAlive(EntityHandle handle) { if(token == 0) return false; Access(handle); var status = api.Alive(handle); if (status == 2) return false; Check(status); return true; }
    internal void Destroy(EntityHandle handle) { Access(handle); WriteAccess(); Check(api.Destroy(handle)); }
    internal bool TryTransform(EntityHandle handle, out LocalTransform value)
    {
        Access(handle); LocalTransform copy = default; var status = api.GetTransform(handle, &copy); value = copy;
        if (status == 2) return false; Check(status); return true;
    }
    internal LocalTransform Transform(EntityHandle handle) => TryTransform(handle, out var value) ? value : throw new InvalidOperationException("Entity or Transform is no longer valid.");
    internal void Transform(EntityHandle handle, LocalTransform value) { Access(handle); WriteAccess(); Check(api.SetTransform(handle, &value)); }
    internal bool TryScript(EntityHandle handle, out ScriptState value)
    {
        Access(handle); ScriptState copy = default; var status = scripts.Get(handle, &copy); value = copy;
        if (status == 2) return false; Check(status); return true;
    }
    internal ScriptState Script(EntityHandle handle) => TryScript(handle, out var value) ? value : throw new InvalidOperationException("ScriptComponent is no longer attached.");
    internal void EnableScript(EntityHandle handle, bool value) { Access(handle); WriteAccess(); Check(scripts.Enable(handle, value ? 1u : 0u)); }
    internal void SetScript(EntityHandle handle, string type, bool enabled)
    {
        Access(handle); WriteAccess(); var bytes = Encoding.UTF8.GetBytes(type);
        fixed (byte* data = bytes) Check(scripts.Set(handle, new NativeString { Data = data, Length = checked((uint)bytes.Length) }, enabled ? 1u : 0u));
    }
    internal void RemoveScript(EntityHandle handle) { Access(handle); WriteAccess(); Check(scripts.Remove(handle)); }
}
