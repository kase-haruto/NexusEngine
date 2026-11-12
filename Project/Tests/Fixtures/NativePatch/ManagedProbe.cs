using Nexus.Managed;
using System.Numerics;

namespace NativePatchFixture;
/*-----------------------------------------------------------------------------------------
 * ManagedProbe
 * - 実Nativeパッチをまたいで数値Bindingを保持し、再登録後の自動解決を検証する
 * - Native pointerは保持せず、Entityの座標を成功回数の値として使用する
 *---------------------------------------------------------------------------------------*/
[ScriptType("smoke.nativepatch")]
public sealed class ManagedProbe : ScriptBehaviour
{
    private NativeFunction? setTarget; //< このBehaviour / Worldの寿命内だけ保持する数値Binding
    protected override void OnCreate() => setTarget = World.BindNativeFunction("Game", "CrowdSimulation", 1, "SetTarget", 1, NativeType.Void, NativeType.Entity, NativeType.Vector3);
    protected override void OnUpdate(float deltaTime)
    {
        // 旧世代のHandleを明示Refreshせず使い、失効時の安全な再解決を通す。
        setTarget!.Invoke(NativeValue.From(Entity), NativeValue.From(new Vector3(3, 0, 0)));
        var transform = Entity.Transform;
        // Native Crowdが更新するtranslationから分離し、呼出し成功回数をscale値で観測する。
        transform.Scale.Y += 1;
        Entity.Transform = transform;
    }
}
