#include "AnimationPlayerComponent.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace NexusEngine {
namespace {
float AdvanceTime(float time, float deltaTime, float speed) noexcept {
    const double next = static_cast<double>(time) + static_cast<double>(deltaTime) * speed;
    return static_cast<float>((std::min)(next, static_cast<double>((std::numeric_limits<float>::max)())));
}
}
bool AnimationPlayerComponent::IsValid() const noexcept {
    return std::isfinite(playbackSpeed) && playbackSpeed >= 0 && std::isfinite(time) && time >= 0 &&
        std::isfinite(blendTime) && blendTime >= 0 && std::isfinite(blendWeight) && blendWeight >= 0 && blendWeight <= 1 &&
        std::isfinite(fadeDuration) && fadeDuration >= 0 && std::isfinite(fadeElapsed) && fadeElapsed >= 0;
}
void AnimationPlayerComponent::ClearBlend() noexcept {
    blendAnimationAsset.clear();
    blendAnimationIndex = 0;
    blendTime = 0;
    blendWeight = 0;
    fadeDuration = 0;
    fadeElapsed = 0;
}
void AnimationPlayerComponent::Play(uint32_t clip) noexcept {
    samples.clear();
    runtimeDriven = false;
    animationIndex = clip;
    animationAsset.clear();
    time = 0;
    playing = true;
    ClearBlend();
}
void AnimationPlayerComponent::PlayAsset(const std::string& asset) {
    const std::string reference = asset;
    Play();
    animationAsset = reference;
}
void AnimationPlayerComponent::Pause() noexcept {
    playing = false;
}
void AnimationPlayerComponent::Resume() noexcept {
    playing = true;
}
void AnimationPlayerComponent::Stop() noexcept {
    time = 0;
    playing = false;
    ClearBlend();
}
void AnimationPlayerComponent::CrossFade(uint32_t clip, float seconds, bool targetLoop) noexcept {
    if(!std::isfinite(seconds) || seconds < 0) {
        return;
    }
    if(seconds == 0) {
        Play(clip);
        loop = targetLoop;
        return;
    }
    ClearBlend();
    blendAnimationIndex = clip;
    blendLoop = targetLoop;
    fadeDuration = seconds;
    playing = true;
}
void AnimationPlayerComponent::CrossFadeAsset(const std::string& asset, float seconds, bool targetLoop) {
    const std::string reference = asset;
    if(!std::isfinite(seconds) || seconds < 0) {
        return;
    }
    CrossFade(0, seconds, targetLoop);
    if(seconds == 0) {
        animationAsset = reference;
    } else {
        blendAnimationAsset = reference;
    }
}
void AnimationPlayerComponent::Advance(float deltaTime) noexcept {
    if(!playing || !IsValid() || !std::isfinite(deltaTime) || deltaTime < 0) {
        return;
    }
    time = AdvanceTime(time, deltaTime, playbackSpeed);
    if(blendWeight > 0 || fadeDuration > 0) {
        blendTime = AdvanceTime(blendTime, deltaTime, playbackSpeed);
    }
    if(fadeDuration > 0) {
        // フェード時間は再生速度から独立。PauseとWorldの時間倍率には従う。
        fadeElapsed = AdvanceTime(fadeElapsed, deltaTime, 1);
        blendWeight = (std::min)(fadeElapsed / fadeDuration, 1.0f);
        if(blendWeight >= 1) {
            animationIndex = blendAnimationIndex;
            animationAsset = std::move(blendAnimationAsset);
            time = blendTime;
            loop = blendLoop;
            ClearBlend();
        }
    }
}
}
