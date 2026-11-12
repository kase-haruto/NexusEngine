using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Nexus.Managed;
using Nexus.Managed.Interop;

namespace Nexus.Managed.Bootstrap;
/* NativeのNexusManagedGameplayContextV2と対応する借用値。V1のLayout・entryは変更しない。 */
[StructLayout(LayoutKind.Sequential, Pack = 8)]
public struct GameplayContext
{
    public uint AbiVersion, StructSize;
    public ulong World;
    public NativeGameApi Game;
    public NativeScriptApi Scripts;
    public NativeString GameAssembly;
}
/*-----------------------------------------------------------------------------------------
 * ReflectedGameplayContext
 * - V2を変更せず、Property状態のcopy APIを末尾へ追加するV3の借用値
 * - Native所有物を参照せず、開始処理で関数表とパスをコピーする
 *---------------------------------------------------------------------------------------*/
[StructLayout(LayoutKind.Sequential, Pack = 8)]
public struct ReflectedGameplayContext
{
    public GameplayContext Gameplay; //< V2と同じprefix。ABI番号と全体sizeはV3の値を要求する
    public NativeScriptPropertyApi Properties; //< Componentアドレスを返さないcopy API
}
/* V3のprefixを維持し、共通World servicesを値で追加するV4。 */
[StructLayout(LayoutKind.Sequential, Pack = 8)]
public struct HybridGameplayContext { public ReflectedGameplayContext Gameplay; public NativeGameServicesApi Services; }
/* V4と既存entryは維持する。V5にはEngine常駐のBinding表だけを追加する。 */
[StructLayout(LayoutKind.Sequential, Pack = 8)]
public struct BoundGameplayContext { public HybridGameplayContext Gameplay; public NativeBindingApi Bindings; }

public static unsafe partial class EntryPoints
{
    /// <summary>追加Scriptの同期と寿命検証をサポートするBootstrapだけが成功を返す。</summary>
#if !NEXUS_MONO
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
#endif
    public static int SupportsScriptAttachments(uint abi) => abi == 1 ? Ok : IncompatibleAbi;
    /// <summary>安定APIを共有してGameScriptsをロードし、既存WorldのGameplayセッションを開始する。</summary>
#if !NEXUS_MONO
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
#endif
    public static int StartGameplay(GameplayContext* context, ulong* sessionId)
        => StartGameplayCore(context, sessionId, default);

    /// <summary>V3の値copy契約を検証する。PropertyInfoやManaged参照をNativeへ渡さない。</summary>
#if !NEXUS_MONO
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
#endif
    public static int StartReflectedGameplay(ReflectedGameplayContext* context, ulong* sessionId)
    {
        if (sessionId == null) return InvalidArgument; *sessionId = 0;
        if (context == null || context->Gameplay.AbiVersion != 3 || context->Gameplay.StructSize != sizeof(ReflectedGameplayContext) ||
            sizeof(ReflectedGameplayContext) != 160 || sizeof(NativeScriptPropertyApi) != 24 || context->Properties.AbiVersion != 1 ||
            context->Properties.StructSize != 24 || context->Properties.Read == null || context->Properties.Write == null) return IncompatibleAbi;
        var compatible = context->Gameplay; compatible.AbiVersion = 2; compatible.StructSize = 136;
        return StartGameplayCore(&compatible, sessionId, context->Properties);
    }
    /// <summary>Native / Managedが同じWorldの値APIを使うV4開始処理。旧entryとLayoutは維持する。</summary>
#if !NEXUS_MONO
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
#endif
    public static int StartHybridGameplay(HybridGameplayContext* context, ulong* sessionId)
        => StartHybridGameplayCore(context, sessionId);

    /// <summary>全DTOのlayoutを確認し、Native Systemの安全な数値dispatchをWorldへ接続する。</summary>
#if !NEXUS_MONO
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
#endif
    public static int StartBoundGameplay(BoundGameplayContext* context, ulong* sessionId)
    {
        if(sessionId == null) return InvalidArgument; *sessionId = 0;
        if(context == null || sizeof(BoundGameplayContext) != 240 || sizeof(NativeBindingApi) != 24 || sizeof(NativeCallValue) != 56 || sizeof(NativeBindingHandle) != 24 ||
            sizeof(NativeApiType) != 24 || sizeof(NativeApiParameter) != 40 || sizeof(NativeFunctionDescriptor) != 64 ||
            context->Gameplay.Gameplay.Gameplay.AbiVersion != 5 || context->Gameplay.Gameplay.Gameplay.StructSize != 240 ||
            context->Bindings.AbiVersion != 1 || context->Bindings.StructSize != 24 || context->Bindings.Resolve == null || context->Bindings.Invoke == null) return IncompatibleAbi;
        var compatible = context->Gameplay; compatible.Gameplay.Gameplay.AbiVersion = 4; compatible.Gameplay.Gameplay.StructSize = 216;
        return StartHybridGameplayCore(&compatible, sessionId, context->Bindings);
    }
    private static int StartHybridGameplayCore(HybridGameplayContext* context, ulong* sessionId, NativeBindingApi bindings = default)
    {
        if(sessionId == null) return InvalidArgument; *sessionId = 0;
        if(context == null || sizeof(HybridGameplayContext) != 216 || context->Gameplay.Gameplay.AbiVersion != 4 || context->Gameplay.Gameplay.StructSize != 216 ||
            sizeof(NativeGameServicesApi) != 56 || sizeof(NativeGameEvent) != 40 || sizeof(NativeBytes) != 16 ||
            context->Services.AbiVersion != 1 || context->Services.StructSize != 56 || context->Services.Open == null || context->Services.Close == null ||
            context->Services.Register == null || context->Services.Invoke == null || context->Services.Publish == null || context->Services.Poll == null ||
            context->Gameplay.Properties.AbiVersion != 1 || context->Gameplay.Properties.StructSize != 24 || context->Gameplay.Properties.Read == null || context->Gameplay.Properties.Write == null) return IncompatibleAbi;
        var compatible = context->Gameplay.Gameplay; compatible.AbiVersion = 2; compatible.StructSize = 136;
        return StartGameplayCore(&compatible, sessionId, context->Gameplay.Properties, context->Services, bindings);
    }
    private static int StartGameplayCore(GameplayContext* context, ulong* sessionId, NativeScriptPropertyApi properties, NativeGameServicesApi services = default, NativeBindingApi bindings = default)
    {
        if (sessionId == null) return InvalidArgument; *sessionId = 0;
        if (context == null || context->AbiVersion != 2 || context->StructSize != sizeof(GameplayContext) || sizeof(GameplayContext) != 136 ||
            sizeof(NativeGameApi) != 64 || sizeof(NativeScriptApi) != 40 || sizeof(ScriptBinding) != 56 || sizeof(LocalTransform) != 40 ||
            context->World == 0 || context->Game.AbiVersion != 1 || context->Game.StructSize != 64 || context->Scripts.AbiVersion != 1 || context->Scripts.StructSize != 40 ||
            context->Game.Create == null || context->Game.Destroy == null || context->Game.Alive == null || context->Game.Entities == null ||
            context->Game.GetTransform == null || context->Game.SetTransform == null || context->Game.Log == null ||
            context->Scripts.Set == null || context->Scripts.Remove == null || context->Scripts.Get == null || context->Scripts.Enable == null) return IncompatibleAbi;
        Session? session = null; ulong id = 0;
        try
        {
            session = new Session(new BootstrapContext { World = context->World, Log = context->Game.Log });
            session.GameApi = context->Game; session.ScriptApi = context->Scripts; session.PropertyApi = properties;
            session.ServicesApi = services; session.BindingApi = bindings;
            session.World = session.CreateWorld();
            session.Catalog = new GameScriptCatalog(ScriptRuntime.Text(context->GameAssembly));
#if NEXUS_MONO
            session.NextInstance = monoInstanceSeed;
            session.Gameplay = new ScriptRuntime(session.World, session.Catalog.Assemblies, session.NextInstance);
#else
            session.Gameplay = new ScriptRuntime(session.World, session.Catalog.Assemblies);
#endif
            var status = session.Write("Managed gameplay session started.");
            if (status != Ok) { session.Clear(); return status; }
            lock (SessionRegistry.Gate)
            {
                if (SessionRegistry.Next == 0) { session.Clear(); return Failed; }
                id = SessionRegistry.Next; SessionRegistry.Next = unchecked(id + 1); SessionRegistry.Sessions.Add(id, session);
            }
            *sessionId = id; return Ok;
        }
        catch (Exception exception)
        {
            try { if (id != 0) { lock (SessionRegistry.Gate) SessionRegistry.Sessions.Remove(id); } } catch { }
            Report(session, exception); session?.Clear(); return Failed;
        }
    }
    /// <summary>一回のInteropでScript全体を同期・更新する。Native snapshotへの参照は保存しない。</summary>
#if !NEXUS_MONO
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
#endif
    public static int Tick(ulong sessionId, ScriptBinding* bindings, uint count, float deltaTime, uint initializeOnly)
    {
        if ((count != 0 && bindings == null) || count > int.MaxValue || float.IsNaN(deltaTime) || float.IsInfinity(deltaTime) || deltaTime < 0 || initializeOnly > 1) return InvalidArgument;
        Session? session = null;
        try
        {
            lock (SessionRegistry.Gate)
            {
                if (!SessionRegistry.Sessions.TryGetValue(sessionId, out session)) return InvalidHandle;
                if (session.Thread != Environment.CurrentManagedThreadId) return WrongThread;
                if (session.Busy || session.Suspended || session.CleanupFailed || session.Gameplay == null || (session.ReloadCommitting && initializeOnly == 0)) return Failed;
                session.Busy = true;
            }
            try { return session.Gameplay.Tick(new Span<ScriptBinding>(bindings, checked((int)count)), deltaTime, initializeOnly != 0) ? Ok : Failed; }
            finally { session.Busy = false; }
        }
        catch (Exception exception) { Report(session, exception); return Failed; }
    }
}
