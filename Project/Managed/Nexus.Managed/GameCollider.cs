using System.Numerics;
namespace Nexus.Managed;

/// <summary>Triangle MeshはStatic/Kinematic専用。動く複雑な形状はConvexMeshを使う。</summary>
public enum ColliderShape { Box = 0, Sphere = 1, Capsule = 2, Mesh = 3, ConvexMesh = 4 }
/// <summary>32個の衝突レイヤー。両方のColliderのmaskが相手を許可したときだけ衝突する。</summary>
public static class CollisionLayers
{
    public const uint Everything = uint.MaxValue;
    public const uint Nothing = 0;
    public static uint Mask(params int[] layers)
    {
        if(layers == null) throw new ArgumentNullException(nameof(layers));
        uint mask = 0;
        foreach(var layer in layers) {
            if(layer < 0 || layer > 31) throw new ArgumentOutOfRangeException(nameof(layers));
            mask |= 1u << layer;
        }
        return mask;
    }
}
/// <summary>共通ECSのColliderを操作する。Collider単体はStatic、RigidBody併用で運動する。</summary>
public sealed class GameCollider
{
    public GameComponent Component { get; }
    public GameCollider(Entity entity) { Component = entity.GetComponent("Collider"); }
    public ColliderShape Shape {
        get => (ColliderShape)Component.Get<int>("shape");
        set {
            if(value == ColliderShape.Mesh && Component.Entity.TryGetComponent("RigidBody", out var body) && body!.Get<int>("motion") == 1)
                throw new InvalidOperationException("Triangle Mesh requires a Static or Kinematic RigidBody. Use ConvexMesh for Dynamic bodies.");
            Component.Set("shape", (int)value);
        }
    }
    public string MeshAsset { get => Component.Get<string>("meshAsset"); set => Component.Set("meshAsset", value); }
    public Vector3 HalfExtent { get => Component.Get<Vector3>("halfExtent"); set => Component.Set("halfExtent", value); }
    public float Radius { get => Component.Get<float>("radius"); set => Component.Set("radius", value); }
    public float HalfHeight { get => Component.Get<float>("halfHeight"); set => Component.Set("halfHeight", value); }
    public bool IsTrigger { get => Component.Get<bool>("sensor"); set => Component.Set("sensor", value); }
    public int Layer { get => Component.Get<int>("collisionLayer"); set => Component.Set("collisionLayer", value); }
    public uint CollisionMask { get => unchecked((uint)Component.Get<int>("collisionMask")); set => Component.Set("collisionMask", unchecked((int)value)); }
    /// <summary>形状とmeshを検証して追加する。既存Colliderがある場合も設定する。</summary>
    public static GameCollider Add(Entity entity, ColliderShape shape = ColliderShape.Box, string meshAsset = "")
    {
        if(shape < ColliderShape.Box || shape > ColliderShape.ConvexMesh) throw new ArgumentOutOfRangeException(nameof(shape));
        if(shape >= ColliderShape.Mesh && string.IsNullOrEmpty(meshAsset)) throw new ArgumentException("Mesh shapes require a collision mesh asset.", nameof(meshAsset));
        if(shape == ColliderShape.Mesh && entity.TryGetComponent("RigidBody", out var body) && body!.Get<int>("motion") == 1)
            throw new InvalidOperationException("Triangle Mesh requires a Static or Kinematic RigidBody. Use ConvexMesh for Dynamic bodies.");
        var existed = entity.HasComponent("Collider");
        try {
            entity.AddComponent("Collider"); var collider = new GameCollider(entity);
            if(shape >= ColliderShape.Mesh) collider.MeshAsset = meshAsset;
            collider.Shape = shape; return collider;
        } catch { if(!existed && entity.IsAlive) entity.RemoveComponent("Collider"); throw; }
    }
}
