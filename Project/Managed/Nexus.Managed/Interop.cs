using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
[assembly: InternalsVisibleTo("Nexus.Managed.Bootstrap")]
[assembly: InternalsVisibleTo("Nexus.Managed.Metadata")]

namespace Nexus.Managed.Interop;

/* 公開C ABIと同じPOD宣言。構造体のポインタは呼出し中だけ借用し、Entity / Componentアドレスを含まない。 */
[StructLayout(LayoutKind.Sequential, Pack = 8)]
public unsafe struct NativeString {
#if NEXUS_MONO
    private IntPtr data;
    public byte* Data { get => (byte*)data; set => data = (IntPtr)value; }
#else
    public byte* Data;
#endif
    public uint Length;
}
[StructLayout(LayoutKind.Sequential, Pack = 8)]
public struct EntityHandle : IEquatable<EntityHandle>
{
    internal ulong World;
    internal uint Index, Generation;
    public readonly bool Equals(EntityHandle other) => World == other.World && Index == other.Index && Generation == other.Generation;
    public override readonly bool Equals(object? other) => other is EntityHandle handle && Equals(handle);
#if NEXUS_MONO
    public override readonly int GetHashCode() => unchecked((World.GetHashCode() * 397 ^ (int)Index) * 397 ^ (int)Generation);
#else
    public override readonly int GetHashCode() => HashCode.Combine(World, Index, Generation);
#endif
}
#if !NEXUS_MONO
[StructLayout(LayoutKind.Sequential, Pack = 8)]
public unsafe struct NativeGameApi
{
    public uint AbiVersion, StructSize;
    public delegate* unmanaged[Cdecl]<ulong, NativeString, EntityHandle*, int> Create;
    public delegate* unmanaged[Cdecl]<EntityHandle, int> Destroy;
    public delegate* unmanaged[Cdecl]<EntityHandle, int> Alive;
    public delegate* unmanaged[Cdecl]<ulong, EntityHandle*, uint, uint*, int> Entities;
    public delegate* unmanaged[Cdecl]<EntityHandle, LocalTransform*, int> GetTransform;
    public delegate* unmanaged[Cdecl]<EntityHandle, LocalTransform*, int> SetTransform;
    public delegate* unmanaged[Cdecl]<ulong, NativeString, int> Log;
}
#endif
[StructLayout(LayoutKind.Sequential, Pack = 8)]
public struct ScriptState { public ulong Attachment; public uint Enabled, Reserved; }
#if !NEXUS_MONO
[StructLayout(LayoutKind.Sequential, Pack = 8)]
public unsafe struct NativeScriptApi
{
    public uint AbiVersion, StructSize;
    public delegate* unmanaged[Cdecl]<EntityHandle, NativeString, uint, int> Set;
    public delegate* unmanaged[Cdecl]<EntityHandle, int> Remove;
    public delegate* unmanaged[Cdecl]<EntityHandle, ScriptState*, int> Get;
    public delegate* unmanaged[Cdecl]<EntityHandle, uint, int> Enable;
}
#endif
[StructLayout(LayoutKind.Sequential, Pack = 8)]
public unsafe struct ScriptBinding
{
    public EntityHandle Entity;
    public NativeString Type;
    public ulong Attachment, Instance;
    public uint Enabled, Reserved;
}
/* Property状態のコピー専用API。V1 ScriptApiのLayoutは維持する。 */
#if !NEXUS_MONO
[StructLayout(LayoutKind.Sequential, Pack = 8)]
public unsafe struct NativeScriptPropertyApi
{
    public uint AbiVersion, StructSize;
    public delegate* unmanaged[Cdecl]<EntityHandle, ulong, byte*, uint, uint*, int> Read;
    public delegate* unmanaged[Cdecl]<EntityHandle, ulong, NativeString, int> Write;
}
#endif
/* 共通services ABI。登録callbackはNative clientに限定し、Managed delegateをNativeへ渡さない。 */
[StructLayout(LayoutKind.Sequential, Pack = 8)]
public unsafe struct NativeBytes {
#if NEXUS_MONO
    private IntPtr data;
    public byte* Data { get => (byte*)data; set => data = (IntPtr)value; }
#else
    public byte* Data;
#endif
    public uint Length;
}
[StructLayout(LayoutKind.Sequential, Pack = 8)]
public struct NativeGameEvent { public ulong Sequence; public EntityHandle Source; public uint Version, TypeLength, PayloadLength, Reserved; }
#if !NEXUS_MONO
[StructLayout(LayoutKind.Sequential, Pack = 8)]
public unsafe struct NativeGameServicesApi
{
    public uint AbiVersion, StructSize;
    public delegate* unmanaged[Cdecl]<ulong, ulong*, int> Open;
    public delegate* unmanaged[Cdecl]<ulong, int> Close;
    public delegate* unmanaged[Cdecl]<ulong, NativeString, uint, delegate* unmanaged[Cdecl]<void*, EntityHandle, NativeBytes, byte*, uint, uint*, int>, void*, int> Register;
    public delegate* unmanaged[Cdecl]<ulong, NativeString, uint, EntityHandle, NativeBytes, byte*, uint, uint*, int> Invoke;
    public delegate* unmanaged[Cdecl]<ulong, NativeString, uint, EntityHandle, NativeBytes, int> Publish;
    public delegate* unmanaged[Cdecl]<ulong, NativeGameEvent*, byte*, uint, uint*, int> Poll;
}
#endif
