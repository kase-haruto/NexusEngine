#pragma once
#include "Foundation/Reflection/ReflectionMarkers.h"
#include "Runtime/Animation/AnimationPlayback.h"
#include "Graphics/Model/ModelAnimation.h"
#include "Runtime/Animation/AnimationLayers.h"
#include "Runtime/Animation/AnimationControllerPlayer.h"
#include "Runtime/Animation/AnimationPoseAssets.h"
#include <string>

NEXUS_REFLECT_COMPONENT(SkeletalAnimationComponent, "SkeletalAnimation", "component.skeletalAnimation", clipAsset, speedCurveAsset, playbackSpeed, loop, playing, elapsed, speedCurveDuration, blendClipAsset, blendSpeed, blendLoop, blendWeight, crossfadeDuration, layerAsset, controllerAsset, controllerParameters, rootMotion, rootMotionJoint, rootMotionAxes, rootMotionRotation, applyRootMotion, reverseEvents)
NEXUS_COMPONENT_EDITOR(SkeletalAnimationComponent, "Animation", "componentBrowser.description.skeletal", "details.Animation")

namespace NexusEngine {
/** \brief 骨格clipの二入力再生状態。CPU pose評価はModelInstanceへ渡しGPU資源は持たない。 */
struct SkeletalAnimationComponent {
    std::string clipAsset, speedCurveAsset; //< 主clipと速度curveの相対参照
    float playbackSpeed = 1; //< 主clip基本速度
    bool loop = true, playing = true; //< 主clipのloopと両clockの再生要求
    float elapsed = 0, speedCurveDuration = 0; //< 独立経過秒とcurve区間。0区間はclip長
    std::string blendClipAsset; //< 空なら単一clip
    float blendSpeed = 1; //< 副clip独立速度
    bool blendLoop = true; //< 副clip独立loop
    float blendWeight = 0; //< 手動blend。0..1
    float crossfadeDuration = .2f; //< 次のCrossFadeに用いる秒数
    AnimationPlayback playback, blendPlayback; //< Entity専有clock
    float blendElapsed = 0, transitionElapsed = 0; //< 遷移の独立経過秒
    bool transitioning = false; //< 手動blendとcrossfadeを区別する
    std::vector<AnimationPoseSample> samples; //< Rendererへ渡すCPU sample。assetの一部ではない
    std::vector<AnimationPoseSample> frozenSource; //< 割込時点の混合入力を固定し、姿勢の連続性を保つ
    std::string activeClip, activeBlend; //< 再選択検出。保存しない
    std::string layerAsset; //< 順序付きlayer定義.nxlayers
    std::shared_ptr<const AnimationLayerDocument> activeLayers; //< 共有不変設定
    std::vector<AnimationLayerPlayback> layers; //< Entity専有の独立clock
    std::string controllerAsset, controllerParameters = "{}";
    AnimationControllerPlayer controller;
    std::string activeController;
    bool runtimeDriven = false;
    bool timelineDriven = false; //< frame内のTimeline所有権。controller時計は借用中に進めない
    std::vector<ControllerEventRange> timelineRanges;
    double controlledBeginElapsed = 0; //< controller更新前のlayer初期化基準
    bool rootMotion = false, rootMotionRotation = true;
    std::string rootMotionJoint; //< targetモデル内の長さ付き階層path
    Vector3 rootMotionAxes{1, 0, 1}; //< model空間の移動成分。0でposeへ残す、1でEntityへ移す
    std::vector<RootMotionRange> rootRanges; //< 今回の更新で進んだ範囲。Seekでは空
    bool applyRootMotion = true; //< falseなら差分を公開するだけで、ゲーム側が物理等へ渡す
    RootMotionDelta extractedRootMotion; //< frameごとの差分。assetやLevelには保存しない
    bool reverseEvents = false; //< 逆方向のイベント通知を明示許可する
    bool IsValid() const;
    void Play();
    void Pause();
    void Stop();
    /** \brief 共通の独立時刻へ移動する。遷移中は両clockとfade経過の相対差を維持する。 */
    void Seek(float seconds);
    /** \brief 割込時は現在の混合姿勢を固定したsourceとする。Assetそのものは変更しない。 */
    void CrossFade(const std::string& target, float seconds, float targetSpeed = 1, bool targetLoop = true);
};
}
