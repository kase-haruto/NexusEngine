using System.Numerics;
using System.Runtime.InteropServices;
using System.Text;
using Nexus.Managed.Interop;

namespace Nexus.Managed
{
    /// <summary>Public ABIの値種別。任意objectやNative pointerは渡さない。</summary>
    public enum NativeValueKind : uint { Void, Bool, Int32, UInt32, Float32, Vector2, Vector3, Vector4, Utf8, Entity, Enum32, Float32Array }
    /// <summary>型付きFacadeの署名。EnumはC++型名に依存しない安定IDを指定する。</summary>
    public readonly record struct NativeType(NativeValueKind Kind, string? EnumId = null)
    {
        public static NativeType Void => new(NativeValueKind.Void);
        public static NativeType Entity => new(NativeValueKind.Entity);
        public static NativeType Vector3 => new(NativeValueKind.Vector3);
    }
    /// <summary>Native APIの失敗。未登録・寿命・互換性・Thread違反をstatusで判別できる。</summary>
    public sealed class NativeApiException : InvalidOperationException
    {
        public int Status { get; }
        internal NativeApiException(int status) : base($"Native System API failed with status {status}.") { Status = status; }
    }
    /*-----------------------------------------------------------------------------------------
     * NativeValue
     * - Native呼出し用の安全な値を保持する。MarshalとpinはGameWorld内部だけで行う
     * - 配列は呼出し時にコピーする。NativeがManaged配列やobjectを保存する契約ではない
     *---------------------------------------------------------------------------------------*/
    public readonly struct NativeValue
    {
        public NativeType Type { get; }
        internal object? Payload { get; }
        private NativeValue(NativeType type, object? payload) { Type = type; Payload = payload; }
        public T As<T>() => Payload is T value ? value : throw new InvalidOperationException("Native result has another type.");
        public static NativeValue From(bool value) => new(new(NativeValueKind.Bool), value);
        public static NativeValue From(int value) => new(new(NativeValueKind.Int32), value);
        public static NativeValue From(uint value) => new(new(NativeValueKind.UInt32), value);
        public static NativeValue From(float value) => new(new(NativeValueKind.Float32), value);
        public static NativeValue From(Vector2 value) => new(new(NativeValueKind.Vector2), value);
        public static NativeValue From(Vector3 value) => new(new(NativeValueKind.Vector3), value);
        public static NativeValue From(Vector4 value) => new(new(NativeValueKind.Vector4), value);
        public static NativeValue From(string value) => new(new(NativeValueKind.Utf8), value ?? throw new ArgumentNullException(nameof(value)));
        public static NativeValue From(Entity value) => new(NativeType.Entity, value ?? throw new ArgumentNullException(nameof(value)));
        public static NativeValue From(float[] value) => new(new(NativeValueKind.Float32Array), value ?? throw new ArgumentNullException(nameof(value)));
        public static NativeValue Enum(string typeId, int value) => new(new(NativeValueKind.Enum32, typeId), value);
    }
    /*-----------------------------------------------------------------------------------------
     * NativeFunction
     * - Worldと数値Binding世代だけを保持する。Game DLL関数pointerやSystem objectは保持しない
     * - 再登録後はcallback前の予約statusだけで再解決する。Native実行後の失敗は再送しない
     *---------------------------------------------------------------------------------------*/
    public sealed class NativeFunction
    {
        internal readonly GameWorld Owner; //< session終了後もGameWorldがアクセスを拒否する
        internal NativeBindingHandle Handle; //< scope / System / Binding世代。callback未実行の失効時だけ再解決する
        internal readonly string Module, SystemId, FunctionId;
        internal readonly uint SystemVersion, FunctionVersion; //< 再解決でも署名と互換性番号を省略しない
        internal readonly NativeType Result;
        internal readonly NativeType[] Parameters; //< 呼出元の署名配列からコピーし、後から変更させない
        internal NativeFunction(GameWorld owner, NativeBindingHandle handle, NativeType result, NativeType[] parameters, string module, string system, uint systemVersion, string function, uint functionVersion) {
            Owner = owner; Handle = handle; Result = result; Parameters = parameters.ToArray();
            Module = module; SystemId = system; SystemVersion = systemVersion; FunctionId = function; FunctionVersion = functionVersion;
        }
        /// <summary>現在の署名を再照合する。DLL pointerを返さず、異なるVersionへ自動fallbackしない。</summary>
        public void Refresh() => Handle = Owner.BindNativeFunction(Module, SystemId, SystemVersion, FunctionId, FunctionVersion, Result, Parameters).Handle;
        /// <summary>一回の同期呼出しで値を渡す。例外を扱うのはFacadeまたはゲームの呼出元。</summary>
        public NativeValue Invoke(params NativeValue[] arguments) => Owner.InvokeBinding(this, arguments);
    }
    public sealed unsafe partial class GameWorld
    {
        private NativeBindingApi nativeBindings; //< Engine常駐関数表。Game DLLのコード参照は含めない
        private static void CheckBinding(int status) { if(status != 0) throw new NativeApiException(status); }
        private static NativeApiType CopyType(NativeType type, byte* storage, ref int used)
        {
            if((uint)type.Kind > 11 || (type.Kind != NativeValueKind.Enum32 && type.EnumId != null)) throw new ArgumentException("Unsupported Native signature.");
            NativeApiType result = new() { Kind = (uint)type.Kind };
            if(type.Kind == NativeValueKind.Enum32) {
                var bytes = Encoding.UTF8.GetBytes(type.EnumId ?? throw new ArgumentException("Enum type ID is required."));
                if(bytes.Length is 0 or > 128) throw new ArgumentException("Invalid Enum type ID.");
                bytes.AsSpan().CopyTo(new Span<byte>(storage + used, bytes.Length)); result.TypeId = new() { Data = storage + used, Length = (uint)bytes.Length }; used += bytes.Length;
            }
            return result;
        }
        /// <summary>Module / System / Function / Versionと全署名を照合する。C++内部型の自動公開は行わない。</summary>
        public NativeFunction BindNativeFunction(string module, string system, uint systemVersion, string function, uint functionVersion, NativeType result, params NativeType[] parameters)
        {
            Access(); if(parameters == null) throw new ArgumentNullException(nameof(parameters));
            if(nativeBindings.Resolve == null) throw new InvalidOperationException("Native System bindings are unavailable.");
            if(parameters.Length > 16) throw new ArgumentOutOfRangeException(nameof(parameters));
            var moduleBytes = Encoding.UTF8.GetBytes(module); var systemBytes = Encoding.UTF8.GetBytes(system); var functionBytes = Encoding.UTF8.GetBytes(function);
            var nativeParameters = new NativeApiParameter[parameters.Length]; var enumIds = new byte[4096];
            NativeBindingHandle handle = default;
            fixed(byte* moduleData = moduleBytes) fixed(byte* systemData = systemBytes) fixed(byte* functionData = functionBytes) fixed(byte* types = enumIds) fixed(NativeApiParameter* items = nativeParameters) {
                int used = 0;
                for(int index = 0; index < parameters.Length; ++index) {
                    nativeParameters[index].Type = CopyType(parameters[index], types, ref used);
                    // 引数名は診断用。署名照合は宣言順・型・Enum IDで行う。
                    types[used] = (byte)'p'; types[used + 1] = (byte)('A' + index);
                    nativeParameters[index].Id = new() { Data = types + used, Length = 2 }; used += 2;
                }
                NativeFunctionDescriptor signature = new() { StructSize = (uint)sizeof(NativeFunctionDescriptor), Version = functionVersion,
                    Id = new() { Data = functionData, Length = (uint)functionBytes.Length }, Result = CopyType(result, types, ref used), Parameters = items, ParameterCount = (uint)parameters.Length, OwnerThreadOnly = 1 };
                CheckBinding(nativeBindings.Resolve(token, new() { Data = moduleData, Length = (uint)moduleBytes.Length }, new() { Data = systemData, Length = (uint)systemBytes.Length }, systemVersion, &signature, &handle));
            }
            return new NativeFunction(this, handle, result, parameters, module, system, systemVersion, function, functionVersion);
        }
        private NativeCallValue CopyArgument(NativeValue value, byte* storage)
        {
            NativeCallValue result = new() { Kind = (uint)value.Type.Kind };
            switch(value.Type.Kind) {
                case NativeValueKind.Bool: result.Integer = value.As<bool>() ? 1 : 0; break;
                case NativeValueKind.Int32: case NativeValueKind.Enum32: result.Integer = value.As<int>(); break;
                case NativeValueKind.UInt32: result.Integer = value.As<uint>(); break;
                case NativeValueKind.Float32: result.X = value.As<float>(); break;
                case NativeValueKind.Vector2: var v2 = value.As<Vector2>(); result.X = v2.X; result.Y = v2.Y; break;
                case NativeValueKind.Vector3: var v3 = value.As<Vector3>(); result.X = v3.X; result.Y = v3.Y; result.Z = v3.Z; break;
                case NativeValueKind.Vector4: var v4 = value.As<Vector4>(); result.X = v4.X; result.Y = v4.Y; result.Z = v4.Z; result.W = v4.W; break;
                case NativeValueKind.Entity: var entity = value.As<Entity>(); Access(entity.Handle); result.Entity = entity.Handle; break;
                case NativeValueKind.Utf8:
                    var bytes = new UTF8Encoding(false, true).GetBytes(value.As<string>());
                    if(bytes.Length > 4096) throw new ArgumentOutOfRangeException(nameof(value));
                    bytes.AsSpan().CopyTo(new Span<byte>(storage, bytes.Length)); result.Count = (uint)bytes.Length; result.Data = bytes.Length == 0 ? null : storage; break;
                case NativeValueKind.Float32Array:
                    var array = value.As<float[]>(); if(array.Length > 1024) throw new ArgumentOutOfRangeException(nameof(value));
                    MemoryMarshal.AsBytes(array.AsSpan()).CopyTo(new Span<byte>(storage, array.Length * 4)); result.Count = (uint)array.Length; result.Data = array.Length == 0 ? null : storage; break;
                default: throw new ArgumentException("Void is not a Native argument.");
            }
            return result;
        }
        /// <summary>pinを呼出し中に限定し、戻り値をManaged所有値へコピーしてから解放する。</summary>
        internal NativeValue InvokeBinding(NativeFunction function, NativeValue[] arguments)
        {
            WriteAccess(); if(arguments == null) throw new ArgumentNullException(nameof(arguments));
            if(function.Owner != this || arguments.Length != function.Parameters.Length) throw new ArgumentException("Native signature mismatch.");
            var values = new NativeCallValue[arguments.Length]; var payload = new byte[arguments.Length * 4096]; var output = new byte[4096]; NativeCallValue result = default;
            fixed(byte* storage = payload) fixed(byte* buffer = output) fixed(NativeCallValue* items = values) {
                for(int index = 0; index < arguments.Length; ++index) {
                    if(arguments[index].Type != function.Parameters[index]) throw new ArgumentException("Native argument type mismatch.");
                    values[index] = CopyArgument(arguments[index], storage + index * 4096);
                }
                var status = nativeBindings.Invoke(token, function.Handle, items, (uint)values.Length, &result, buffer, 4096);
                if(status == 10) {
                    // Engineだけがcallback前に返す予約status。副作用後のNative失敗を再送してはならない。
                    function.Refresh();
                    status = nativeBindings.Invoke(token, function.Handle, items, (uint)values.Length, &result, buffer, 4096);
                }
                CheckBinding(status);
                if(result.Kind != (uint)function.Result.Kind || ((result.Kind == 8 || result.Kind == 11) && result.Count != 0 && result.Data != buffer)) throw new InvalidOperationException("Invalid Native result.");
                switch(function.Result.Kind) {
                    case NativeValueKind.Void: return default;
                    case NativeValueKind.Bool: return NativeValue.From(result.Integer != 0);
                    case NativeValueKind.Int32: return NativeValue.From(checked((int)result.Integer));
                    case NativeValueKind.UInt32: return NativeValue.From(checked((uint)result.Integer));
                    case NativeValueKind.Enum32: return NativeValue.Enum(function.Result.EnumId!, checked((int)result.Integer));
                    case NativeValueKind.Float32: return NativeValue.From(result.X);
                    case NativeValueKind.Vector2: return NativeValue.From(new Vector2(result.X, result.Y));
                    case NativeValueKind.Vector3: return NativeValue.From(new Vector3(result.X, result.Y, result.Z));
                    case NativeValueKind.Vector4: return NativeValue.From(new Vector4(result.X, result.Y, result.Z, result.W));
                    case NativeValueKind.Entity: return NativeValue.From(Wrap(result.Entity));
                    case NativeValueKind.Utf8: return NativeValue.From(new UTF8Encoding(false, true).GetString(output, 0, checked((int)result.Count)));
                    case NativeValueKind.Float32Array: return NativeValue.From(MemoryMarshal.Cast<byte, float>(output.AsSpan(0, checked((int)result.Count * 4))).ToArray());
                    default: throw new InvalidOperationException("Unsupported Native result.");
                }
            }
        }
    }
}
namespace Nexus.Managed.Interop
{
    /* ABIの鏡像だけを定義する。これらのpointerをゲームコードの保持対象にしない。 */
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    public unsafe struct NativeCallValue { public uint Kind, Count; public long Integer; public float X, Y, Z, W; public EntityHandle Entity; public byte* Data; }
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    public struct NativeBindingHandle { public ulong Scope, System, Function; }
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    public unsafe struct NativeApiType { public uint Kind, Reserved; public NativeString TypeId; }
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    public unsafe struct NativeApiParameter { public NativeString Id; public NativeApiType Type; }
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    public unsafe struct NativeFunctionDescriptor { public uint StructSize, Version; public NativeString Id; public NativeApiType Result; public NativeApiParameter* Parameters; public uint ParameterCount, OwnerThreadOnly; }
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    public unsafe struct NativeBindingApi {
        public uint AbiVersion, StructSize;
#if NEXUS_MONO
        private IntPtr resolve, invoke;
        public ResolveBindingDelegate Resolve => MonoNativeDelegates.Resolve<ResolveBindingDelegate>(resolve);
        public InvokeBindingDelegate Invoke => MonoNativeDelegates.Resolve<InvokeBindingDelegate>(invoke);
#else
        public delegate* unmanaged[Cdecl]<ulong, NativeString, NativeString, uint, NativeFunctionDescriptor*, NativeBindingHandle*, int> Resolve;
        public delegate* unmanaged[Cdecl]<ulong, NativeBindingHandle, NativeCallValue*, uint, NativeCallValue*, byte*, uint, int> Invoke;
#endif
    }
}
