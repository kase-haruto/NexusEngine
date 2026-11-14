using System.Numerics;
using System.Runtime.InteropServices;
using System.Text;
using Nexus.Managed.Interop;

namespace Nexus.Managed;

/*-----------------------------------------------------------------------------------------
 * ComponentValue / ComponentRequest
 * - GameComponents.hと同じ固定DTO。MonoでもGC参照やpointer fieldを持たせない
 * - 文字列はDTO後続のUTF-8へコピーし、Native ComponentをManaged側に保持しない
 *---------------------------------------------------------------------------------------*/
[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal struct ComponentValue
{
    internal uint Kind, StringLength; //< 境界用kindと後続文字列byte数
    internal int Integer; //< Bool / Int / Enumの値
    internal uint Reserved; //< version 1では0
    internal float X, Y, Z, W; //< Float / Vector値のコピー
}
[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal struct ComponentRequest
{
    internal uint Operation, TypeLength, PropertyLength, Offset, Limit, Reserved; //< GameComponents.hのheader
    internal ComponentValue Value; //< Writeだけが使用する値
}
/*-----------------------------------------------------------------------------------------
 * GameComponent
 * - 共通ECS上の現在のComponentを、Entityと安定Type IDから毎回解決する
 * - instanceやNative pointerを所有しない。削除中は失敗し、再追加後は現在のComponentを参照する
 *---------------------------------------------------------------------------------------*/
public sealed class GameComponent
{
    private readonly Entity entity; //< session寿命のWorld capabilityを借用するwrapper
    public string TypeId { get; } //< class名と独立したReflectionの安定ID
    internal GameComponent(Entity owner, string typeId) { entity = owner; TypeId = typeId; }
    public Entity Entity => entity;
    public ComponentReference Reference => entity.World.Reference(this);
    /// <summary>共通Propertyの値をコピーする。対応型はbool / int / float / string / Vector2～4 / int32 Enum。</summary>
    public unsafe T Get<T>(string propertyId)
    {
        var bytes = entity.World.ComponentCall(4, entity.Handle, TypeId, propertyId);
        if(bytes.Length < sizeof(ComponentValue)) throw new InvalidOperationException("Invalid Component response.");
        ComponentValue value;
        fixed(byte* data = bytes) value = *(ComponentValue*)data;
        if(value.Reserved != 0 || value.StringLength > 3072 || sizeof(ComponentValue) + value.StringLength != bytes.Length)
            throw new InvalidOperationException("Invalid Component value size.");
        object result;
        if(typeof(T) == typeof(bool) && value.Kind == 1 && (value.Integer is 0 or 1)) result = value.Integer != 0;
        else if(typeof(T) == typeof(int) && value.Kind == 2) result = value.Integer;
        else if(typeof(T) == typeof(float) && value.Kind == 3) result = value.X;
        else if(typeof(T) == typeof(EntityReference) && value.Kind == 4) result = EntityReference.Parse(GameWorld.ComponentEncoding.GetString(bytes, sizeof(ComponentValue), checked((int)value.StringLength)));
        else if(typeof(T) == typeof(ComponentReference) && value.Kind == 4) result = ComponentReference.Parse(GameWorld.ComponentEncoding.GetString(bytes, sizeof(ComponentValue), checked((int)value.StringLength)));
        else if(typeof(T) == typeof(string) && value.Kind == 4) result = GameWorld.ComponentEncoding.GetString(bytes, sizeof(ComponentValue), checked((int)value.StringLength));
        else if(typeof(T) == typeof(Vector2) && value.Kind == 5) result = new Vector2(value.X, value.Y);
        else if(typeof(T) == typeof(Vector3) && value.Kind == 6) result = new Vector3(value.X, value.Y, value.Z);
        else if(typeof(T) == typeof(Vector4) && value.Kind == 7) result = new Vector4(value.X, value.Y, value.Z, value.W);
        else if(typeof(T).IsEnum && Enum.GetUnderlyingType(typeof(T)) == typeof(int) && value.Kind == 8) result = Enum.ToObject(typeof(T), value.Integer);
        else throw new InvalidOperationException($"Property {TypeId}.{propertyId} does not match {typeof(T).Name}.");
        return (T)result;
    }
    /// <summary>Detailsと同じsetterで検証して反映する。Play Worldへの変更であり、EditorのUndoは作らない。</summary>
    public void Set<T>(string propertyId, T input)
    {
        ComponentValue value = new() { Reserved = 0 }; string text = "";
        if(input is bool boolean) { value.Kind = 1; value.Integer = boolean ? 1 : 0; }
        else if(input is int integer) { value.Kind = 2; value.Integer = integer; }
        else if(input is float number) { value.Kind = 3; value.X = number; }
        else if(input is EntityReference entityReference) { value.Kind = 4; text = entityReference.ToString(); }
        else if(input is ComponentReference componentReference) { value.Kind = 4; text = componentReference.ToString(); }
        else if(input is string content) { value.Kind = 4; text = content; }
        else if(input is Vector2 vector2) { value.Kind = 5; value.X = vector2.X; value.Y = vector2.Y; }
        else if(input is Vector3 vector3) { value.Kind = 6; value.X = vector3.X; value.Y = vector3.Y; value.Z = vector3.Z; }
        else if(input is Vector4 vector4) { value.Kind = 7; value.X = vector4.X; value.Y = vector4.Y; value.Z = vector4.Z; value.W = vector4.W; }
        else if(input is Enum enumeration && Enum.GetUnderlyingType(enumeration.GetType()) == typeof(int)) { value.Kind = 8; value.Integer = Convert.ToInt32(enumeration); }
        else throw new NotSupportedException("Component values require a supported non-null value type or string.");
        _ = entity.World.ComponentCall(5, entity.Handle, TypeId, propertyId, value, text);
    }
}
/*-----------------------------------------------------------------------------------------
 * GameWorld (components)
 * - 既存GameServicesの固定protocolを型付きManaged APIへ包む
 * - 呼出側はP/Invoke・function pointer・GCHandleを扱わず、同じsession / Thread検証を使う
 *---------------------------------------------------------------------------------------*/
public sealed unsafe partial class GameWorld
{
    internal static readonly UTF8Encoding ComponentEncoding = new(false, true); //< 不正なUTF-16を暗黙置換しない
    internal byte[] ComponentCall(uint operation, EntityHandle target, string typeId, string propertyId = "",
        ComponentValue value = default, string text = "", uint offset = 0, uint limit = 0)
    {
        if(operation is 2 or 3 or 5) WriteAccess(); else Access();
        if(target.World != 0) Access(target);
        if(servicesClient == 0) throw new InvalidOperationException("Component API requires Game services.");
        if(string.IsNullOrEmpty(typeId)) throw new ArgumentException("A stable Component Type ID is required.", nameof(typeId));
        if(propertyId == null) throw new ArgumentNullException(nameof(propertyId));
        if(sizeof(ComponentRequest) != 56 || sizeof(ComponentValue) != 32) throw new InvalidOperationException("Component ABI layout mismatch.");
        var type = ComponentEncoding.GetBytes(typeId); var property = ComponentEncoding.GetBytes(propertyId); var content = ComponentEncoding.GetBytes(text);
        if(type.Length > 512 || property.Length > 512 || content.Length > 3072) throw new ArgumentOutOfRangeException(nameof(typeId), "Component protocol size limit exceeded.");
        value.StringLength = checked((uint)content.Length);
        var header = new ComponentRequest { Operation = operation, TypeLength = (uint)type.Length, PropertyLength = (uint)property.Length,
            Offset = offset, Limit = limit, Reserved = 0, Value = value };
        var request = new byte[checked(sizeof(ComponentRequest) + type.Length + property.Length + content.Length)];
        if(request.Length > 4096) throw new ArgumentOutOfRangeException(nameof(text));
        Buffer.BlockCopy(type, 0, request, sizeof(ComponentRequest), type.Length);
        Buffer.BlockCopy(property, 0, request, sizeof(ComponentRequest) + type.Length, property.Length);
        Buffer.BlockCopy(content, 0, request, sizeof(ComponentRequest) + type.Length + property.Length, content.Length);
        fixed(byte* input = request) *(ComponentRequest*)input = header;
        var result = InvokeWorldCommand("nexus.engine.components", target, request, operation is 2 or 3 or 5);
        if((operation is 2 or 3 or 5) && result.Length != 0) throw new InvalidOperationException("Unexpected Component mutation response.");
        return result;
    }
    /// <summary>共通のsession / Thread / Reload検査後に固定Engine protocolを呼び出す。</summary>
    private byte[] InvokeWorldCommand(string id, EntityHandle target, byte[] request, bool write)
    {
        if(write) WriteAccess(); else Access();
        if(target.World != 0) Access(target);
        if(servicesClient == 0) throw new InvalidOperationException("World API requires Game services.");
        if(request.Length > 4096) throw new ArgumentOutOfRangeException(nameof(request));
        var key = ComponentEncoding.GetBytes(id); var output = new byte[4096]; uint length = 0;
        fixed(byte* input = request) fixed(byte* name = key) fixed(byte* response = output) {
            Check(services.Invoke(servicesClient, new NativeString { Data = name, Length = (uint)key.Length }, 1, target,
                new NativeBytes { Data = input, Length = (uint)request.Length }, response, 4096, &length));
        }
        if(length > 4096) throw new InvalidOperationException("Invalid Component response length.");
        return output.AsSpan(0, (int)length).ToArray();
    }
    /// <summary>最大64 EntityのLocal Transformを一回のInteropでコピーする。</summary>
    public LocalTransform[] GetTransforms(params Entity[] entities)
    {
        if(entities == null) throw new ArgumentNullException(nameof(entities));
        if(entities.Length > 64) throw new ArgumentOutOfRangeException(nameof(entities));
        if(sizeof(EntityHandle) != 16 || sizeof(LocalTransform) != 40) throw new InvalidOperationException("Transform ABI layout mismatch.");
        var request = new byte[8 + entities.Length * 16];
        fixed(byte* data = request) {
            *(uint*)data = 1; *(uint*)(data + 4) = (uint)entities.Length;
            for(var index = 0; index < entities.Length; ++index) {
                var entity = entities[index] ?? throw new ArgumentNullException(nameof(entities)); Access(entity.Handle);
                *(EntityHandle*)(data + 8 + index * 16) = entity.Handle;
            }
        }
        var output = InvokeWorldCommand("nexus.engine.transforms", default, request, false);
        if(output.Length != entities.Length * 40) throw new InvalidOperationException("Invalid Transform batch response.");
        var values = new LocalTransform[entities.Length];
        fixed(byte* data = output)
            for(var index = 0; index < values.Length; ++index) values[index] = *(LocalTransform*)(data + index * 40);
        return values;
    }
    /// <summary>最大64件を全件検証後に更新する。不正Handle / 値を含む場合は一件も反映しない。</summary>
    public void SetTransforms(Entity[] entities, LocalTransform[] values)
    {
        if(entities == null) throw new ArgumentNullException(nameof(entities));
        if(values == null) throw new ArgumentNullException(nameof(values));
        if(entities.Length != values.Length || entities.Length > 64) throw new ArgumentException("Transform batch requires matching arrays of at most 64 items.");
        if(sizeof(EntityHandle) != 16 || sizeof(LocalTransform) != 40) throw new InvalidOperationException("Transform ABI layout mismatch.");
        var request = new byte[8 + entities.Length * 56];
        fixed(byte* data = request) {
            *(uint*)data = 2; *(uint*)(data + 4) = (uint)entities.Length;
            for(var index = 0; index < entities.Length; ++index) {
                var entity = entities[index] ?? throw new ArgumentNullException(nameof(entities)); Access(entity.Handle);
                *(EntityHandle*)(data + 8 + index * 56) = entity.Handle;
                *(LocalTransform*)(data + 24 + index * 56) = values[index];
            }
        }
        if(InvokeWorldCommand("nexus.engine.transforms", default, request, true).Length != 0)
            throw new InvalidOperationException("Unexpected Transform mutation response.");
    }
    /// <summary>既存ECSで指定Componentを持つEntityを列挙する。列挙中に構造変更を挟まない。</summary>
    public IReadOnlyList<Entity> Query(string componentTypeId)
    {
        var result = new List<Entity>(); uint offset = 0;
        for(;;) {
            var page = ComponentCall(6, default, componentTypeId, offset: offset, limit: 255);
            if(page.Length < 8) throw new InvalidOperationException("Invalid Component query header.");
            var count = BitConverter.ToUInt32(page, 0); var next = BitConverter.ToUInt32(page, 4);
            if(count > 255 || page.Length != 8 + count * 16) throw new InvalidOperationException("Invalid Component query size.");
            fixed(byte* data = page)
                for(uint index = 0; index < count; ++index) result.Add(Wrap(*(EntityHandle*)(data + 8 + index * 16)));
            if(next == uint.MaxValue) return result;
            if(count == 0 || next <= offset) throw new InvalidOperationException("Invalid Component query cursor.");
            offset = next;
        }
    }
}
