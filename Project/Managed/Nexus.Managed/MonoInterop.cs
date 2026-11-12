using System.Runtime.InteropServices;

namespace Nexus.Managed.Interop;
/*-----------------------------------------------------------------------------------------
 * MonoNativeDelegates
 * - MonoのCdecl marshallerへEngine常駐APIを接続する
 * - DTO内のIntPtrは関数アドレスだけであり、Entity / Componentのメモリアドレスを含まない
 * - Mono 6.12のfunction-pointer fieldの制約を境界内部で吸収する
 *---------------------------------------------------------------------------------------*/
internal static class MonoNativeDelegates
{
    // nilは入口のABI検査で判定する。通常呼出しへ進む関数表だけを非null契約として扱う。
    internal static T Resolve<T>(IntPtr pointer) where T : Delegate => pointer == IntPtr.Zero ? null! : Marshal.GetDelegateForFunctionPointer<T>(pointer);
}
[UnmanagedFunctionPointer(CallingConvention.Cdecl)] public unsafe delegate int CreateEntityDelegate(ulong world, NativeString name, EntityHandle* entity);
[UnmanagedFunctionPointer(CallingConvention.Cdecl)] public delegate int EntityStatusDelegate(EntityHandle entity);
[UnmanagedFunctionPointer(CallingConvention.Cdecl)] public unsafe delegate int EntitiesDelegate(ulong world, EntityHandle* entities, uint capacity, uint* count);
[UnmanagedFunctionPointer(CallingConvention.Cdecl)] public unsafe delegate int TransformDelegate(EntityHandle entity, LocalTransform* transform);
[UnmanagedFunctionPointer(CallingConvention.Cdecl)] public delegate int NativeLogDelegate(ulong world, NativeString message);
[UnmanagedFunctionPointer(CallingConvention.Cdecl)] public delegate int SetScriptDelegate(EntityHandle entity, NativeString type, uint enabled);
[UnmanagedFunctionPointer(CallingConvention.Cdecl)] public unsafe delegate int GetScriptDelegate(EntityHandle entity, ScriptState* state);
[UnmanagedFunctionPointer(CallingConvention.Cdecl)] public delegate int EnableScriptDelegate(EntityHandle entity, uint enabled);
[UnmanagedFunctionPointer(CallingConvention.Cdecl)] public unsafe delegate int ReadPropertyDelegate(EntityHandle entity, ulong attachment, byte* buffer, uint capacity, uint* count);
[UnmanagedFunctionPointer(CallingConvention.Cdecl)] public delegate int WritePropertyDelegate(EntityHandle entity, ulong attachment, NativeString state);
[UnmanagedFunctionPointer(CallingConvention.Cdecl)] public unsafe delegate int OpenClientDelegate(ulong world, ulong* client);
[UnmanagedFunctionPointer(CallingConvention.Cdecl)] public delegate int CloseClientDelegate(ulong client);
[UnmanagedFunctionPointer(CallingConvention.Cdecl)] public unsafe delegate int RegisterCommandDelegate(ulong client, NativeString id, uint version, IntPtr callback, void* state);
[UnmanagedFunctionPointer(CallingConvention.Cdecl)] public unsafe delegate int InvokeCommandDelegate(ulong client, NativeString id, uint version, EntityHandle entity, NativeBytes request, byte* response, uint capacity, uint* count);
[UnmanagedFunctionPointer(CallingConvention.Cdecl)] public delegate int PublishEventDelegate(ulong client, NativeString type, uint version, EntityHandle source, NativeBytes payload);
[UnmanagedFunctionPointer(CallingConvention.Cdecl)] public unsafe delegate int PollEventDelegate(ulong client, NativeGameEvent* header, byte* buffer, uint capacity, uint* count);
[UnmanagedFunctionPointer(CallingConvention.Cdecl)] public unsafe delegate int ResolveBindingDelegate(ulong world, NativeString module, NativeString system, uint version, NativeFunctionDescriptor* signature, NativeBindingHandle* handle);
[UnmanagedFunctionPointer(CallingConvention.Cdecl)] public unsafe delegate int InvokeBindingDelegate(ulong world, NativeBindingHandle handle, NativeCallValue* arguments, uint count, NativeCallValue* result, byte* buffer, uint capacity);

/*-----------------------------------------------------------------------------------------
 * NativeGameApi / NativeScriptApi / NativeScriptPropertyApi / NativeGameServicesApi (Mono)
 * - CoreCLR版と同じ固定layoutを持つMono向けDTO
 * - delegateは呼出時だけ解決し、DTO内にはGC参照を置かない
 *---------------------------------------------------------------------------------------*/
[StructLayout(LayoutKind.Sequential, Pack = 8)]
public struct NativeGameApi
{
    public uint AbiVersion, StructSize;
    private IntPtr create, destroy, alive, entities, getTransform, setTransform, log;
    public CreateEntityDelegate Create => MonoNativeDelegates.Resolve<CreateEntityDelegate>(create);
    public EntityStatusDelegate Destroy => MonoNativeDelegates.Resolve<EntityStatusDelegate>(destroy);
    public EntityStatusDelegate Alive => MonoNativeDelegates.Resolve<EntityStatusDelegate>(alive);
    public EntitiesDelegate Entities => MonoNativeDelegates.Resolve<EntitiesDelegate>(entities);
    public TransformDelegate GetTransform => MonoNativeDelegates.Resolve<TransformDelegate>(getTransform);
    public TransformDelegate SetTransform => MonoNativeDelegates.Resolve<TransformDelegate>(setTransform);
    public NativeLogDelegate Log => MonoNativeDelegates.Resolve<NativeLogDelegate>(log);
}
[StructLayout(LayoutKind.Sequential, Pack = 8)]
public struct NativeScriptApi
{
    public uint AbiVersion, StructSize;
    private IntPtr set, remove, get, enable;
    public SetScriptDelegate Set => MonoNativeDelegates.Resolve<SetScriptDelegate>(set);
    public EntityStatusDelegate Remove => MonoNativeDelegates.Resolve<EntityStatusDelegate>(remove);
    public GetScriptDelegate Get => MonoNativeDelegates.Resolve<GetScriptDelegate>(get);
    public EnableScriptDelegate Enable => MonoNativeDelegates.Resolve<EnableScriptDelegate>(enable);
}
[StructLayout(LayoutKind.Sequential, Pack = 8)]
public struct NativeScriptPropertyApi
{
    public uint AbiVersion, StructSize;
    private IntPtr read, write;
    public ReadPropertyDelegate Read => MonoNativeDelegates.Resolve<ReadPropertyDelegate>(read);
    public WritePropertyDelegate Write => MonoNativeDelegates.Resolve<WritePropertyDelegate>(write);
}
[StructLayout(LayoutKind.Sequential, Pack = 8)]
public struct NativeGameServicesApi
{
    public uint AbiVersion, StructSize;
    private IntPtr open, close, register, invoke, publish, poll;
    public OpenClientDelegate Open => MonoNativeDelegates.Resolve<OpenClientDelegate>(open);
    public CloseClientDelegate Close => MonoNativeDelegates.Resolve<CloseClientDelegate>(close);
    public RegisterCommandDelegate Register => MonoNativeDelegates.Resolve<RegisterCommandDelegate>(register);
    public InvokeCommandDelegate Invoke => MonoNativeDelegates.Resolve<InvokeCommandDelegate>(invoke);
    public PublishEventDelegate Publish => MonoNativeDelegates.Resolve<PublishEventDelegate>(publish);
    public PollEventDelegate Poll => MonoNativeDelegates.Resolve<PollEventDelegate>(poll);
}
