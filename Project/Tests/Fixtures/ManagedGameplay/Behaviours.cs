using Nexus.Managed;
namespace Fixture;
/* 共通ECS・lifecycle・例外・callback内構造変更をユーザー側で検証するGameScripts。別Worldを作らない。 */
public sealed class Probe : ScriptBehaviour
{
    protected override void OnCreate() => World.Log("Probe.Create");
    protected override void OnEnable() => World.Log("Probe.Enable");
    protected override void OnUpdate(float dt) { var value = Entity.Transform; value.Translation.X += dt; Entity.Transform = value; World.Log("Probe.Update"); }
    protected override void OnDisable() => World.Log("Probe.Disable");
    protected override void OnDestroy() => World.Log("Probe.Destroy");
}
/* 解除失敗がGC完了だけで成功へ戻らず、残りのresourceを解除することを観測する負例。 */
public sealed class CleanupResources : ScriptBehaviour
{
    /* callback参照を保持せず、Dispose順序と失敗だけを既存Loggerへ記録するfixture。 */
    private sealed class Resource(GameWorld world, string marker, bool fail) : IDisposable
    {
        public void Dispose() { world.Log(marker); if (fail) throw new InvalidOperationException("Expected resource cleanup failure."); }
    }
    protected override void OnCreate()
    {
        TrackResource(new Resource(World, "Cleanup.ResourceRemaining", false));
        TrackResource(new Resource(World, "Cleanup.ResourceFailure", true));
    }
    protected override void OnDestroy() => World.Log("Cleanup.ResourcesDestroy");
}
/* OnDisableの失敗が通常Tickの成功に隠れないことを観測する。 */
public sealed class CleanupDisable : ScriptBehaviour
{
    protected override void OnDisable() { World.Log("Cleanup.Disable"); throw new InvalidOperationException("Expected disable cleanup failure."); }
    protected override void OnDestroy() => World.Log("Cleanup.DisableDestroy");
}
/* OnDestroy例外を記録しただけでReload成功にしないためのfixture。 */
public sealed class CleanupDestroy : ScriptBehaviour
{
    protected override void OnDestroy() { World.Log("Cleanup.Destroy"); throw new InvalidOperationException("Expected destroy cleanup failure."); }
}
/* CancellationTokenの解除callbackが失敗しても、OnDestroyと残りの参照解除を続ける。 */
public sealed class CleanupCancellation : ScriptBehaviour
{
    protected override void OnCreate() => TrackResource(Lifetime.Register(() => throw new InvalidOperationException("Expected lifetime cancellation failure.")));
    protected override void OnDestroy() => World.Log("Cleanup.CancellationDestroy");
}
/* Preflightで生成した未Bind候補の解除も監査する。候補の失敗から旧世代を安全に再開できるとは仮定しない。 */
public sealed class CleanupPreflight : ScriptBehaviour
{
    private sealed class Resource : IDisposable
    {
        public void Dispose() => throw new InvalidOperationException("Expected preflight cleanup failure.");
    }
    public CleanupPreflight()
    {
        // Exporterも既定値用にconstructorを呼ぶため、負例の副作用は実行テストのscopeだけで有効にする。
        if(Environment.GetEnvironmentVariable("NEXUS_TEST_CLEANUP_CONSTRUCTORS") == "1") TrackResource(new Resource());
    }
}
/* ALCの終了hook例外も、GC待ちだけで復旧可能な失敗として扱わない。 */
public sealed class CleanupUnload : ScriptBehaviour
{
    public CleanupUnload()
    {
        if(Environment.GetEnvironmentVariable("NEXUS_TEST_CLEANUP_CONSTRUCTORS") != "1") return;
        var context = System.Runtime.Loader.AssemblyLoadContext.GetLoadContext(GetType().Assembly)!;
        context.Unloading += _ => throw new InvalidOperationException("Expected unload callback failure.");
    }
}
/* CLRの大文字小文字を区別しないAssembly名解決でも、Bootstrap内部参照を拒否することを観測する。 */
public sealed class AssemblyBoundaryProbe : ScriptBehaviour
{
    protected override void OnCreate()
    {
        var requested = typeof(ScriptBehaviour).Assembly.GetName(); requested.Name = requested.Name!.ToLowerInvariant();
        var api = System.Reflection.Assembly.Load(requested);
        if(!ReferenceEquals(api, typeof(ScriptBehaviour).Assembly)) throw new InvalidOperationException("Stable Managed API identity was duplicated.");
        try { System.Reflection.Assembly.Load("nexus.managed.bootstrap"); }
        catch(Exception exception) when(exception is FileLoadException or FileNotFoundException) { World.Log("Assembly.BootstrapBlocked"); return; }
        throw new InvalidOperationException("Game Assembly accessed Bootstrap internals by changing name casing.");
    }
}
/* 値コピーとComponent削除後の取得失敗を検証する。Native Componentの参照をキャッシュしない。 */
public sealed class ComponentProbe : ScriptBehaviour
{
    protected override void OnCreate()
    {
        if (!Entity.IsAlive || !Entity.TryGetTransform(out var copy)) throw new InvalidOperationException("Missing common Transform.");
        var original = Entity.Transform.Translation.X;
        copy.Translation.X += 100;
        if (Entity.Transform.Translation.X != original) throw new InvalidOperationException("Transform copy unexpectedly aliases native storage.");
        World.Log("Component.Copy");
    }
    protected override void OnUpdate(float dt)
    {
        if (!Entity.TryGetTransform(out var copy)) { World.Log("Component.Missing"); return; }
        copy.Translation.X += dt;
        Entity.Transform = copy;
    }
}
public sealed class SelfDestroy : ScriptBehaviour
{
    protected override void OnUpdate(float dt) => Entity.Destroy();
    protected override void OnDisable() => World.Log("Self.Disable");
    protected override void OnDestroy() { if(Entity.IsAlive) throw new InvalidOperationException("Destroyed owner revived."); World.Log("Self.Destroy"); }
}
public sealed class Fault : ScriptBehaviour
{
    protected override void OnUpdate(float dt) => throw new InvalidOperationException("Expected script failure.");
    protected override void OnDisable() => World.Log("Fault.Disable");
}
public sealed class RequestStop : ScriptBehaviour
{
    protected override void OnUpdate(float dt) { World.Log("RequestStop"); _ = Entity.Transform; }
    protected override void OnDestroy() => World.Log("RequestStop.Destroy");
}
public sealed class Spawn : ScriptBehaviour
{
    protected override void OnUpdate(float dt)
    {
        World.CreateEntity("Child").SetScript<Probe>();
        Entity.RemoveScript();
    }
    protected override void OnDestroy() => World.Log("Spawn.Destroy");
}
/* 永続ID / Alias・復元順序・frame末snapshotをユーザー実行で確認するfixture。 */
[ScriptType("fixture.reflected", Version = 2, Aliases = new[] { "fixture.old" })]
public sealed class Reflected : ScriptBehaviour
{
    [Expose(Id = "speed", Aliases = new[] { "oldSpeed" })]
    public float Speed { get; set; } = 5.0f;
    [Expose]
    public System.Numerics.Vector3 Offset { get; set; } = System.Numerics.Vector3.One;
    protected override void OnCreate() { var value = Entity.Transform; value.Translation.X = Speed; Entity.Transform = value; }
    protected override void OnUpdate(float dt) { Speed += dt; }
}
/* Reload用の同一型。別fixtureだけ定数を変更し、復元された値で新コードが動くことを確認する。 */
[ScriptType("fixture.hot")]
public sealed class Hot : ScriptBehaviour
{
    [Expose(Id = "speed")]
    public float Speed { get; set; } = 3;
    protected override void OnCreate()
    {
#if NEXUS_RELOAD_FAIL
        Entity.Destroy(); // 失敗候補の構造変更が元Worldへ流れないことを検証する。
        throw new InvalidOperationException("Expected new generation initialization failure.");
#elif NEXUS_RELOAD_V2
        World.Log("Hot.V2");
#else
        World.Log("Hot.V1");
#endif
    }
    protected override void OnUpdate(float dt)
    {
        var value = Entity.Transform;
#if NEXUS_RELOAD_V2
        value.Translation.X += Speed * dt * 2;
#else
        value.Translation.X += Speed * dt;
#endif
        Entity.Transform = value;
    }
    protected override void OnDestroy() => World.Log("Hot.Destroy");
}
/* 故意にworker / GCHandleを残す負例。ユーザーテストがflagを作るまでUnloadを妨げる。 */
public sealed class Leaky : ScriptBehaviour
{
    protected override void OnCreate()
    {
        var file = Environment.GetEnvironmentVariable("NEXUS_TEST_LEAK_RELEASE_FILE") ?? throw new InvalidOperationException("Missing leak test flag.");
        if(File.Exists(file)) return;
        var root = System.Runtime.InteropServices.GCHandle.Alloc(this);
        new Thread(() => {
            try { while (!File.Exists(file)) Thread.Sleep(10); }
            finally { root.Free(); }
        }) { IsBackground = true }.Start();
    }
    // Lifetimeを無視してworkerを残す誤実装を再現する。通常のゲームコードでは禁止する。
    protected override void OnDestroy() { }
}
/* 既存Native fixtureと同じEntityを操作し、opaque counterと値Eventを検証する。 */
public sealed class Hybrid : ScriptBehaviour
{
    protected override void OnCreate()
    {
#if NEXUS_RELOAD_FAIL
        _ = World.InvokeNative("fixture.bump", 1, Entity, ReadOnlySpan<byte>.Empty);
#endif
    }
    protected override void OnUpdate(float dt)
    {
        var response = World.InvokeNative("fixture.bump", 1, Entity, ReadOnlySpan<byte>.Empty);
        var value = Entity.Transform; value.Translation.X = BitConverter.ToUInt32(response, 0); Entity.Transform = value;
        if(!World.TryPollEvent(out var notification) || notification!.Type != "fixture.bumped" || notification.Source != Entity)
            throw new InvalidOperationException("Native event did not preserve the shared Entity handle.");
        World.PublishEvent("fixture.ack", 1, Entity, response);
    }
}
/* 型付きNative System呼出しの統合fixture。安全な値APIだけを使い、fnptr / GCHandleは扱わない。 */
[ScriptType("fixture.bound")]
public sealed class BoundSystem : ScriptBehaviour
{
    private NativeFunction? setTarget; //< Managed instanceと同じ寿命の数値Binding。staticへ保存しない
    protected override void OnCreate() => setTarget = World.BindNativeFunction("Fixture", "FixtureSystem", 1, "SetTarget", 1, NativeType.Void, NativeType.Entity, NativeType.Vector3);
    protected override void OnUpdate(float dt)
    {
        setTarget!.Invoke(NativeValue.From(Entity), NativeValue.From(new System.Numerics.Vector3(2, 0, 7)));
        World.Log("Bound.Invoked");
    }
}
