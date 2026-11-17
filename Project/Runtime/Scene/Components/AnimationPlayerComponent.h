#pragma once
#include "Foundation/Reflection/ReflectionMarkers.h"
#include <cstdint>
#include <string>
#include "Graphics/Model/ModelAnimation.h"

NEXUS_REFLECT_COMPONENT(AnimationPlayerComponent, "AnimationPlayer", "component.animationPlayer", animationIndex, playbackSpeed, loop, playing, time, animationAsset, blendAnimationAsset, blendAnimationIndex, blendTime, blendLoop, blendWeight, fadeDuration, fadeElapsed)

namespace NexusEngine {
/** 再生状態はEntityが所有する。Asset参照を優先し、番号は既存Levelとの互換用に残す。 */
struct AnimationPlayerComponent {
    uint32_t animationIndex = 0;
    float playbackSpeed = 1.0f;
    bool loop = true;
    bool playing = true;
    float time = 0.0f;
    std::string animationAsset;
    std::string blendAnimationAsset;
    uint32_t blendAnimationIndex = 0;
    float blendTime = 0.0f;
    bool blendLoop = true;
    float blendWeight = 0.0f;
    float fadeDuration = 0.0f;
    float fadeElapsed = 0.0f;

    bool samplesAdvancing = true; //< シーク・一時停止をイベント発火から区別する
    bool runtimeDriven = false; //< 自動追加した再生器は区間外で既定のclip 0を再生しない
    std::vector<AnimationPoseSample> samples; //< Animator/Timelineの評価結果。Level保存へ含めない。
    bool IsValid() const noexcept;
    void Play(uint32_t clip = 0) noexcept;
    void PlayAsset(const std::string& asset);
    void Pause() noexcept;
    void Resume() noexcept;
    void Stop() noexcept;
    /** 現在と次の両クリップを進めながら、指定秒数で次へ移行する。 */
    void CrossFade(uint32_t clip, float seconds, bool targetLoop = true) noexcept;
    void CrossFadeAsset(const std::string& asset, float seconds, bool targetLoop = true);
    void Advance(float deltaTime) noexcept;
private:
    void ClearBlend() noexcept;
};
}
