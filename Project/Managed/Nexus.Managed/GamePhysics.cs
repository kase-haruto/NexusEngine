using System.Numerics;
using System.Runtime.InteropServices;
using Nexus.Managed.Interop;

namespace Nexus.Managed;
/*-----------------------------------------------------------------------------------------
 * PhysicsHit
 * - 共通Native WorldへのRaycast結果を保持する値。Jolt objectを所有しない
 * - Entityは取得後に破棄され得るため、使用時もEntity.IsAliveで検査する
 *---------------------------------------------------------------------------------------*/
public readonly struct PhysicsHit
{
    public Entity? Entity { get; }
    public Vector3 Point { get; }
    public Vector3 Normal { get; }
    public float Distance { get; }
    internal PhysicsHit(Entity entity, Vector3 point, Vector3 normal, float distance) {
        Entity=entity; Point=point; Normal=normal; Distance=distance;
    }
}
/*-----------------------------------------------------------------------------------------
 * PhysicsRequest / NativePhysicsHit
 * - GamePhysics.hと同じ固定レイアウト。Boolean・STL・Native pointerを含めない
 * - requestのbufferは同期Interop呼出し中だけNativeへ借用する
 *---------------------------------------------------------------------------------------*/
[StructLayout(LayoutKind.Sequential, Pack=8)]
internal struct PhysicsRequest
{
    public uint Operation, Reserved;
    public Vector3 Vector, Direction;
    public float Distance;
    public uint Reserved2;
}
[StructLayout(LayoutKind.Sequential, Pack=8)]
internal struct NativePhysicsHit
{
    public EntityHandle Entity;
    public Vector3 Point, Normal;
    public float Distance;
    public uint Reserved;
}
/*-----------------------------------------------------------------------------------------
 * PhysicsContact
 * - 最後のPhysics stepの接触を値で保持する。Exit時はEntityが既に破棄されている場合がある
 * - Native solverの参照を保持せず、利用時に各Entity.IsAliveで寿命を確認する
 *---------------------------------------------------------------------------------------*/
public readonly struct PhysicsContact
{
    public Entity First { get; }
    public Entity Second { get; }
    public Vector3 Point { get; }
    public Vector3 Normal { get; }
    public PhysicsContactKind Kind { get; }
    public bool IsSensor { get; }
    internal PhysicsContact(Entity first, Entity second, Vector3 point, Vector3 normal, PhysicsContactKind kind, bool sensor) {
        First=first; Second=second; Point=point; Normal=normal; Kind=kind; IsSensor=sensor;
    }
}
public enum PhysicsContactKind : uint { Enter=1, Stay=2, Exit=3 }
[StructLayout(LayoutKind.Sequential, Pack=8)]
internal struct NativePhysicsContact
{
    public EntityHandle First, Second;
    public Vector3 Point, Normal;
    public uint Kind, Sensor;
}
[StructLayout(LayoutKind.Sequential, Pack=8)]
internal struct PhysicsContactsHeader { public uint Count, Next, Dropped, Reserved; }

/// <summary>下向きの地面照会結果。斜面の上限を超えた場合も接触面の値を保持する。</summary>
public readonly struct PhysicsGroundHit
{
    public PhysicsHit Hit { get; }
    public float SlopeDegrees { get; }
    public bool IsWalkable { get; }

    internal PhysicsGroundHit(PhysicsHit hit, float slopeDegrees, bool isWalkable)
    {
        Hit = hit;
        SlopeDegrees = slopeDegrees;
        IsWalkable = isWalkable;
    }
}

public sealed unsafe partial class GameWorld
{
    /// <summary>最後のPhysics stepの非消費snapshot。最大4096件。容量超過はdroppedで通知する。</summary>
    public PhysicsContact[] GetPhysicsContacts(out uint dropped)
    {
        var contacts=new List<PhysicsContact>(); uint offset=0; dropped=0;
        do {
            var bytes=PhysicsCall(default,new PhysicsRequest { Operation=4, Reserved=offset },false);
            if(bytes.Length<16 || sizeof(NativePhysicsContact)!=64 || sizeof(PhysicsContactsHeader)!=16)
                throw new InvalidOperationException("Physics contacts ABI mismatch.");
            PhysicsContactsHeader header;
            fixed(byte* pointer=bytes) {
                header=*(PhysicsContactsHeader*)pointer;
                if(header.Reserved!=0 || header.Count>63 || bytes.Length!=16+header.Count*64 || contacts.Count+header.Count>4096)
                    throw new InvalidOperationException("Invalid Physics contacts response.");
                for(uint i=0;i<header.Count;++i) {
                    var value=*(NativePhysicsContact*)(pointer+16+i*64);
                    if(value.Kind<1 || value.Kind>3 || value.Sensor>1) throw new InvalidOperationException("Invalid Physics contact.");
                    // Wrapは値Handleだけを関連付ける。Exitの破棄済みEntityもここでは解決しない。
                    contacts.Add(new PhysicsContact(Wrap(value.First),Wrap(value.Second),value.Point,value.Normal,(PhysicsContactKind)value.Kind,value.Sensor!=0));
                }
            }
            dropped=header.Dropped;
            if(header.Next==uint.MaxValue) break;
            if(header.Next<=offset) throw new InvalidOperationException("Invalid Physics contacts page.");
            offset=header.Next;
        } while(true);
        return contacts.ToArray();
    }
    /// <summary>共通Event queueへの接触通知を有効にする。購読時は毎frame TryPollEventでqueueを消費する。</summary>
    public void EnablePhysicsContactEvents(bool enabled=true)
    {
        if(PhysicsCall(default,new PhysicsRequest { Operation=5, Reserved=enabled ? 1u : 0u },true).Length!=0)
            throw new InvalidOperationException("Unexpected Physics subscription response.");
    }
    /// <summary>既存ECSのColliderへ最近傍Raycastする。方向はNativeで正規化され、無衝突時はfalse。</summary>
    public bool Raycast(Vector3 origin, Vector3 direction, float distance, out PhysicsHit hit)
        => Raycast(origin, direction, distance, uint.MaxValue, out hit);
    /// <summary>所属layerがmaskに含まれるColliderだけを照会する。mask=0は常に無衝突。</summary>
    public bool Raycast(Vector3 origin, Vector3 direction, float distance, uint layerMask, out PhysicsHit hit)
    {
        return Raycast(origin, direction, distance, layerMask, true, null, out hit);
    }

    /// <summary>判定前にTriggerおよび指定した同WorldのEntityを除外し、残りから最近傍を返す。</summary>
    public bool Raycast(Vector3 origin, Vector3 direction, float distance, uint layerMask,
        bool includeTriggers, Entity? exclude, out PhysicsHit hit)
    {
        var target = exclude?.Handle ?? default;
        if (exclude != null)
            Access(target);
        // 従来の引数だけを使う場合は旧Engineと同じ操作番号を維持する。
        bool filtered = !includeTriggers || exclude != null;
        uint operation = filtered ? 7u : layerMask == uint.MaxValue ? 1u : 6u;
        var bytes = PhysicsCall(target, new PhysicsRequest {
            Operation = operation, Reserved = filtered && includeTriggers ? 1u : 0u,
            Vector = origin, Direction = direction, Distance = distance,
            Reserved2 = operation == 1 ? 0u : layerMask
        }, false);
        if(bytes.Length!=48 || sizeof(NativePhysicsHit)!=48) throw new InvalidOperationException("Physics hit ABI mismatch.");
        NativePhysicsHit value;
        fixed(byte* pointer=bytes) value=*(NativePhysicsHit*)pointer;
        if(value.Reserved!=0) throw new InvalidOperationException("Invalid Physics hit response.");
        hit=default;
        if(value.Entity.World==0) return false;
        hit=new PhysicsHit(Wrap(value.Entity),value.Point,value.Normal,value.Distance);
        return true;
    }
    /// <summary>足元のプローブ原点からY下方へ地面を探し、面の傾斜を返す。</summary>
    /// <returns>上向きの面を検出した場合true。歩行可能かはIsWalkableで判定する。</returns>
    public bool ProbeGround(Vector3 origin, float distance, float maxSlopeDegrees, out PhysicsGroundHit ground,
        Entity? exclude = null, uint layerMask = uint.MaxValue)
    {
        if (!(maxSlopeDegrees >= 0 && maxSlopeDegrees <= 90))
            throw new ArgumentOutOfRangeException(nameof(maxSlopeDegrees));

        ground = default;
        // Triggerと自身は探索前に外す。面が上を向く条件を使い、壁・天井を接地候補から外す。
        // 形状内部からの距離0のhitは面に到達した結果ではない。原点は足元より少し上に置く。
        if (!Raycast(origin, -Vector3.UnitY, distance, layerMask, false, exclude, out var hit) ||
            !(hit.Distance > 0) || !(hit.Normal.Y > 0))
        {
            return false;
        }

        // Yは面法線と上方向の内積。丸め誤差でacosの定義域を超えないよう上限を設ける。
        float slopeDegrees = (float)(Math.Acos(Math.Min(hit.Normal.Y, 1f)) * (180.0 / Math.PI));
        ground = new PhysicsGroundHit(hit, slopeDegrees, slopeDegrees <= maxSlopeDegrees);
        return true;
    }

    /// <summary>Dynamic RigidBodyへ次stepの力（World座標、N）を追加する。</summary>
    public void AddForce(Entity entity, Vector3 force) => PushPhysics(2,entity,force);
    /// <summary>Dynamic RigidBodyへ次stepの力積（World座標、N s）を追加する。</summary>
    public void AddImpulse(Entity entity, Vector3 impulse) => PushPhysics(3,entity,impulse);
    private void PushPhysics(uint operation, Entity entity, Vector3 vector)
    {
        if(entity==null) throw new ArgumentNullException(nameof(entity));
        Access(entity.Handle);
        if(PhysicsCall(entity.Handle,new PhysicsRequest { Operation=operation, Vector=vector },true).Length!=0)
            throw new InvalidOperationException("Unexpected Physics mutation response.");
    }
    /// <summary>既存の所有thread・session・Reload書込検査を通し、同じWorldへ値だけを渡す。</summary>
    private byte[] PhysicsCall(EntityHandle target, PhysicsRequest request, bool write)
    {
        if(sizeof(PhysicsRequest)!=40) throw new InvalidOperationException("Physics request ABI mismatch.");
        var bytes=new byte[40];
        fixed(byte* pointer=bytes) *(PhysicsRequest*)pointer=request;
        return InvokeWorldCommand("nexus.engine.physics",target,bytes,write);
    }
}
