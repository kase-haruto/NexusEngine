#pragma once
#include "Foundation/Reflection/ReflectionMarkers.h"
#include "Runtime/Animation/AnimationPlayback.h"
#include "Runtime/Animation/AnimationControllerPlayer.h"
#include <string>

NEXUS_REFLECT_COMPONENT(KeyframeAnimationComponent, "KeyframeAnimation", "component.keyframeAnimation", clipAsset, speedCurveAsset, playbackSpeed, loop, playing, elapsed, speedCurveDuration, controllerAsset, controllerParameters, reverseEvents)
NEXUS_COMPONENT_EDITOR(KeyframeAnimationComponent, "Animation", "componentBrowser.description.keyframe", "details.Animation")

namespace NexusEngine {
/** \brief Entityの数値プロパティclipを再生する。骨格姿勢と描画資源は所有しない。 */
struct KeyframeAnimationComponent {
    std::string clipAsset; //< Assets相対.nxanim
    std::string speedCurveAsset; //< 空なら等速。共有.curve参照
    float playbackSpeed = 1; //< 基本速度。負値で逆再生
    bool loop = true; //< clip時刻をwrapする
    bool playing = true; //< ユーザーの再生要求
    float elapsed = 0; //< 速度に依存しない経過秒。scrubの入力
    float speedCurveDuration = 0; //< 0はclip長
    AnimationPlayback playback; //< Entity専有のclock。Reflection/assetには保存しない
    std::string activeClip; //< 再選択検出。保存しない
    std::string controllerAsset, controllerParameters = "{}";
    AnimationControllerPlayer controller;
    std::string activeController;
    bool runtimeDriven = false;
    bool timelineDriven = false; //< frame内のTimeline所有権。controller時計は借用中に進めない
    std::vector<ControllerEventRange> timelineRanges;
    std::vector<AnimationPoseSample> samples;
    bool reverseEvents = false; //< 逆方向のイベント通知を明示許可する
    bool IsValid() const;
    void Play();
    void Pause();
    void Stop();
    /** \brief 独立経過秒へ移動する。再生要求を変更せず次の評価で積分し直す。 */
    void Seek(float seconds);
};
}
