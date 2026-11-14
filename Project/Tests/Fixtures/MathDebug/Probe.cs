using Nexus.Managed;
using System.Numerics;

[ScriptType("fixture.math-debug")]
public sealed class MathDebugProbe : ScriptBehaviour
{
    [Expose] public EntityReference SavedEntity { get; set; }
    [Expose] public ComponentReference SavedTransform { get; set; }
    [Expose] public int FixedCalls { get; set; }
    [Expose] public int TimerCalls { get; set; }
    [Expose] public int UnscaledCalls { get; set; }
    private int updates, fixedAtCreate;
    private bool afterException;
    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
    protected override void OnCreate()
    {
        Require(Time.Frame == 0 || Time.Elapsed > 0, "Clock did not survive Reload.");
        if(SavedEntity.IsEmpty) SavedEntity = Entity.Reference;
        if(SavedTransform.IsEmpty) SavedTransform = Entity.GetComponent("Transform").Reference;
        Require(SavedEntity.Resolve(World)!.Equals(Entity), "Persistent Entity reference failed.");
        Require(SavedTransform.Resolve(World)!.Entity.Equals(Entity), "Persistent Component reference failed.");
        Require(EntityReference.Parse(SavedEntity.ToString()) == SavedEntity, "Reference string did not round trip.");
        var removed = World.CreateEntity("Reference removed");
        var removedRef = removed.Reference;
        var removedComponent = removed.AddComponent("Fog").Reference;
        removed.RemoveComponent("Fog");
        Require(removedComponent.Resolve(World) == null, "Removed Component reference remained live.");
        removed.AddComponent("Fog");
        Require(removedComponent.Resolve(World) != null, "Component reference did not resolve the current Component.");
        removed.Destroy();
        Require(removedRef.Resolve(World) == null, "Deleted Entity reference reused a slot.");
        var colliderEntity = World.CreateEntity("Collider facade");
        var collider = GameCollider.Add(colliderEntity, ColliderShape.Sphere);
        collider.Layer = 31; collider.CollisionMask = CollisionLayers.Mask(0, 31); collider.Radius = .25f;
        Require(collider.Shape == ColliderShape.Sphere && collider.Layer == 31 && collider.CollisionMask == 0x80000001u, "Collider facade lost layer bits.");
        colliderEntity.Destroy();
        fixedAtCreate = FixedCalls;
        After(0, () => throw new InvalidOperationException("TimerProbe.Exception"));
        After(0, () => afterException = true);
        After(.05, () => ++TimerCalls);
        Every(.05, () => ++UnscaledCalls, true);
        var cancelled = After(0, () => throw new Exception("Cancelled timer fired."));
        cancelled.Dispose();
        Require(Vector3.Distance(Entity.WorldPosition, new Vector3(12, 0, 0)) < 1e-4f, "World matrix convention differs from Native.");
        var point = new Vector3(1, 2, 3);
        Require(Vector3.Distance(Entity.InverseTransformPoint(Entity.TransformPoint(point)), point) < 1e-4f, "Coordinate round trip failed.");
        Require(Vector3.Distance(Entity.TransformNormal(Vector3.UnitZ), Vector3.UnitX) < 1e-4f, "Native rotation and Managed normal differ.");
        Debug.Log("MathDebug.Info");
        Debug.LogWarning("MathDebug.Warning");
        Debug.LogError("MathDebug.Error");
        Require(!Debug.Assert(false, "MathDebug.Assert"), "Assert result was incorrect.");
        try { throw new InvalidOperationException("MathDebug.Exception"); }
        catch (Exception exception) { Debug.LogException(exception); }
        Debug.LogWarning(new string('あ', 2000));
        Task.Run(() =>
        {
            try { Debug.DrawLine(Vector3.Zero, Vector3.One, Vector4.One); }
            catch (InvalidOperationException) { return; }
            throw new Exception("Wrong-thread drawing was accepted.");
        }).GetAwaiter().GetResult();
        Debug.DrawLine(Vector3.Zero, Vector3.UnitX, Vector4.One, 3);
        Debug.DrawRay(Vector3.Zero, Vector3.UnitY, Vector4.One, 3, false);
        Debug.DrawBox(Vector3.Zero, Vector3.One, Quaternion.Identity, Vector4.One, 3);
        Debug.DrawSphere(Vector3.Zero, 1, Vector4.One, 3);
        Debug.DrawAxes(Entity.WorldMatrix, 1, 3);
    }
    protected override void OnFixedUpdate(float fixedDeltaTime)
    {
        Require(Time.InFixedUpdate && Math.Abs(Time.DeltaTime - fixedDeltaTime) < 1e-6, "Fixed clock disagrees with callback.");
        ++FixedCalls;
    }
    protected override void OnUpdate(float deltaTime)
    {
        ++updates;
        Require(!Time.InFixedUpdate && Math.Abs(Time.DeltaTime - deltaTime) < 1e-6, "Variable clock disagrees with callback.");
        if(updates == 1) {
            Require(FixedCalls == fixedAtCreate + 6, "0.1 seconds must produce six 60Hz fixed callbacks.");
            World.TimeScale = 0;
        }
        if(updates == 2) {
            Require(afterException, "Timer exception prevented later callbacks.");
            Require(deltaTime == 0 && FixedCalls == fixedAtCreate + 6, "TimeScale 0 advanced game time.");
            World.TimeScale = 1;
        }
        Debug.Clear();
        Debug.DrawLine(Vector3.Zero, Vector3.UnitX, Vector4.One);
        Debug.Log("MathDebug.Update");
    }
}
