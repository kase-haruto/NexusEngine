using Nexus.Managed;
using System;
using System.Linq;

namespace MonoFixture;
/*-----------------------------------------------------------------------------------------
 * Probe
 * - Mono Domainの起動・共通Transform操作・公開値復元を確認するfixture
 * - Domain差替え時にも同じEntityを操作し、独自のWorldやNative pointerを持たない
 *---------------------------------------------------------------------------------------*/
[ScriptType("mono.probe")]
public sealed class Probe : ScriptBehaviour
{
    [Expose(Id = "speed")]
    public float Speed { get; set; } = 1;
    protected override void OnCreate()
    {
        // Monoからも専用Inspectorを経由せず、既存Fog Componentの共通Propertyを操作する。
        var fog = Entity.AddComponent("Fog");
        fog.Set("startDistance", 3.0f);
        if(fog.Get<float>("startDistance") != 3 || !Entity.HasComponent("Fog")) throw new InvalidOperationException("Shared Component API failed.");
        if(!World.Query("Transform").Contains(Entity)) throw new InvalidOperationException("Shared Component query failed.");
        var values = World.GetTransforms(Entity);
        World.SetTransforms(new[] { Entity }, values);
        World.Log("Mono probe created.");
    }
    protected override void OnUpdate(float deltaTime)
    {
        var transform = Entity.Transform;
#if MONO_PROBE_V2
        transform.Translation.X += Speed * deltaTime * 2;
#else
        transform.Translation.X += Speed * deltaTime;
#endif
        Entity.Transform = transform;
    }
}
/*-----------------------------------------------------------------------------------------
 * SecondaryProbe
 * - 同じEntityの別ScriptとしてY軸を更新し、独立した有効状態と公開値を検証する
 *---------------------------------------------------------------------------------------*/
[ScriptType("mono.secondary")]
public sealed class SecondaryProbe : ScriptBehaviour
{
    [Expose(Id = "lift")] public float Lift { get; set; } = 2;
    protected override void OnCreate() {
        if(Entity.GetScripts().Length != 2) throw new InvalidOperationException("Script list lost an attachment.");
        if(!Entity.AddScript<SecondaryProbe>().IsAttached || Entity.GetScripts().Length != 2)
            throw new InvalidOperationException("Idempotent AddScript duplicated the attachment.");
        if(Lift != 4) throw new InvalidOperationException("Secondary public state was not restored before OnCreate.");
    }
    protected override void OnUpdate(float deltaTime) {
        var transform = Entity.Transform; transform.Translation.Y += Lift * deltaTime; Entity.Transform = transform;
        Enabled = false;
        if(!Entity.ScriptEnabled) throw new InvalidOperationException("Secondary Enabled changed the primary Script.");
    }
}
/*-----------------------------------------------------------------------------------------
 * RepeatedProbe
 * - 同型の複数instanceと、Managedからの共通Input/Physics/Audioを実Backendで確認する
 * - AudioはUpdateで開始し、Reload中のOnCreateで不可逆な副作用を要求しない
 *---------------------------------------------------------------------------------------*/
[ScriptType("mono.repeated")]
public sealed class RepeatedProbe : ScriptBehaviour
{
    [Expose(Id="step")] public float Step { get; set; }=2;
    private AudioVoice? voice;
    protected override void OnCreate() {
        if(Entity.GetScripts().Count(script=>script.TypeId=="mono.repeated")!=2) throw new InvalidOperationException("Repeated instances were merged.");
        if(!World.ReadInput().Down('W')) throw new InvalidOperationException("Managed Input did not read the shared frame.");
        var pads=World.ReadGamepads();
        if(pads.Count!=4 || !pads.Focused || !pads[0].Connected || !pads[0].Down(GamepadButton.A) || pads[0].LeftStick.X!=1 || pads[0].RightTrigger!=1 || pads[1].Connected)
            throw new InvalidOperationException("Managed Gamepad did not read the shared frame.");
        try { _=pads[4]; throw new InvalidOperationException("Gamepad slot validation failed."); } catch(ArgumentOutOfRangeException) { }
        if(!World.AssetExists("silence.wav")) throw new InvalidOperationException("Managed asset lookup did not use the Project Root.");
        var collider=Entity.AddComponent("Collider"); collider.Set("shape",1); collider.Set("radius",0.5f);
        var origin=Entity.Transform.Translation+new System.Numerics.Vector3(0,5,0);
        if(!World.Raycast(origin,new(0,-1,0),10,out var hit) || !Entity.Equals(hit.Entity)) throw new InvalidOperationException("Managed Physics did not query the shared collider.");
    }
    protected override void OnUpdate(float deltaTime) {
        voice ??= World.PlayAudio("silence.wav",0,true);
        if(!voice.IsPlaying) throw new InvalidOperationException("Managed Audio did not start playback.");
        // ColliderだけのStatic Entityでは接触なし。空pageも同じ固定ABIで照会できる。
        if(World.GetPhysicsContacts(out var dropped).Length!=0 || dropped!=0)
            throw new InvalidOperationException("Managed contact snapshot was invalid.");
        if(World.ReadInput().Pressed('L')) World.RequestLevel("Next.level");
        var transform=Entity.Transform;
#if MONO_PROBE_V2
        transform.Translation.Z+=Step*deltaTime*2;
#else
        transform.Translation.Z+=Step*deltaTime;
#endif
        Entity.Transform=transform;
    }
    protected override void OnDestroy() { voice?.Dispose(); }
}
