using System.Numerics;
using System.Runtime.InteropServices;
using Nexus.Managed.Interop;

namespace Nexus.Managed;
/*-----------------------------------------------------------------------------------------
 * LocalTransform
 * - 既存TransformComponentのLocal値のコピー。MatrixとNative Componentを所有しない
 *---------------------------------------------------------------------------------------*/
[StructLayout(LayoutKind.Sequential, Pack = 8)]
public struct LocalTransform
{
    public Vector3 Translation;
    public Quaternion Rotation;
    public Vector3 Scale;
    public static LocalTransform Identity => new() { Rotation = Quaternion.Identity, Scale = Vector3.One };
}
/*-----------------------------------------------------------------------------------------
 * Entity
 * - 共通Worldの世代付きEntity Handleを操作するManaged wrapper
 * - Native Entity / Componentポインタを保持せず、アクセスのたびにEngineで解決する
 *---------------------------------------------------------------------------------------*/
public sealed class Entity : IEquatable<Entity>
{
    private readonly GameWorld world; //< セッション終了で失効するManaged capability
    internal readonly EntityHandle Handle;
    internal Entity(GameWorld owner, EntityHandle handle) { world = owner; Handle = handle; }
    public GameWorld World => world;
    public EntityReference Reference => world.Reference(this);
    /// <summary>Reflectionの安定Type IDで現在のComponentの有無を確認する。</summary>
    public bool HasComponent(string typeId) {
        var bytes = World.ComponentCall(1, Handle, typeId);
        if(bytes.Length != 4) throw new InvalidOperationException("Invalid Component presence response.");
        var present = BitConverter.ToUInt32(bytes, 0);
        if(present > 1) throw new InvalidOperationException("Invalid Component presence value.");
        return present == 1;
    }
    public GameComponent GetComponent(string typeId) => HasComponent(typeId) ? new(this, typeId) : throw new InvalidOperationException($"Missing Component: {typeId}");
    public bool TryGetComponent(string typeId, out GameComponent? component) { component = HasComponent(typeId) ? new(this, typeId) : null; return component != null; }
    /// <summary>既存Sceneへ既定値で追加する。必須Component / Scriptは専用APIを使う。</summary>
    public GameComponent AddComponent(string typeId) { _ = World.ComponentCall(2, Handle, typeId); return new(this, typeId); }
    public void RemoveComponent(string typeId) => World.ComponentCall(3, Handle, typeId);
    public bool IsAlive => world.IsAlive(Handle);
    public bool HasTransform => World.TryTransform(Handle, out _);
    /// <summary>既存TransformComponentのLocal値を取得する。Entity破棄・Component不在ならfalse。返す値の変更は明示的にTransformへ書き戻す。</summary>
    public bool TryGetTransform(out LocalTransform value) => World.TryTransform(Handle, out value);
    public LocalTransform Transform { get => World.Transform(Handle); set => World.Transform(Handle, value); }
    public Matrix4x4 LocalMatrix { get { var value = Transform; return GameMath.TRS(value.Translation, value.Rotation, value.Scale); } }
    /// <summary>現在のLocal値と親階層から計算したコピー。Engine側のmatrix cache更新を待たない。</summary>
    public Matrix4x4 WorldMatrix => World.ReadWorldMatrix(this);
    public Vector3 WorldPosition => GameMath.TransformPoint(WorldMatrix, Vector3.Zero);
    public Vector3 TransformPoint(Vector3 point) => GameMath.TransformPoint(WorldMatrix, point);
    public Vector3 InverseTransformPoint(Vector3 point) => GameMath.TransformPoint(GameMath.Inverse(WorldMatrix), point);
    public Vector3 TransformVector(Vector3 vector) => GameMath.TransformVector(WorldMatrix, vector);
    public Vector3 TransformNormal(Vector3 normal) => GameMath.TransformNormal(WorldMatrix, normal);
    /// <summary>共通ECSのDynamic RigidBodyへ力を追加する。Body pointerは保持しない。</summary>
    public void AddForce(Vector3 force) => World.AddForce(this, force);
    /// <summary>共通ECSのDynamic RigidBodyへ力積を追加する。</summary>
    public void AddImpulse(Vector3 impulse) => World.AddImpulse(this, impulse);
    public bool HasScript => World.TryScript(Handle, out _);
    public bool ScriptEnabled { get => World.Script(Handle).Enabled != 0; set => World.EnableScript(Handle, value); }
    /// <summary>主Scriptを設定・置換する旧API。追加Scriptは維持する。追加にはAddScriptを使う。</summary>
    public void SetScript<T>(bool enabled = true) where T : ScriptBehaviour {
        // 固定IDを優先し、classのrenameで関連付けを失わない。未宣言型は従来のFullNameを使う。
        var type = typeof(T);
        var id = (Attribute.GetCustomAttribute(type, typeof(ScriptTypeAttribute)) as ScriptTypeAttribute)?.Id ?? type.FullName!;
        World.SetScript(Handle, id, enabled);
    }
    /// <summary>従来互換として全Scriptを解除する。個別解除はGameScript.Removeを使う。</summary>
    public void RemoveScript() => World.RemoveScript(Handle);
    /// <summary>別TypeのScriptを追加する。同じ正規IDがあれば既存関連付けを返す。</summary>
    public GameScript AddScript<T>(bool enabled = true) where T : ScriptBehaviour {
        return World.AddScript(this, typeof(T), enabled);
    }
    /// <summary>同じ型でも独立したScript instanceを追加する。公開値とEnabledは別々に保持する。</summary>
    public GameScript AddScriptInstance<T>(bool enabled = true) where T : ScriptBehaviour => World.AddScript(this,typeof(T),enabled,true);
    /// <summary>保存された順序で関連付けを取得する。Behaviour object自体は公開しない。</summary>
    public GameScript[] GetScripts() => World.Scripts(this);
    public void Destroy() => World.Destroy(Handle);
    public bool Equals(Entity? other) => other is not null && Handle.Equals(other.Handle);
    public override bool Equals(object? other) => other is Entity entity && Equals(entity);
    public override int GetHashCode() => Handle.GetHashCode();
    public static bool operator ==(Entity? left, Entity? right) => ReferenceEquals(left, right) || (left is not null && left.Equals(right));
    public static bool operator !=(Entity? left, Entity? right) => !(left == right);
}
