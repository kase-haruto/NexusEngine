#include "SkeletalAnimationComponent.h"
#include <cmath>
#include <algorithm>

namespace NexusEngine {
bool SkeletalAnimationComponent::IsValid() const {
    return std::isfinite(rootMotionAxes.x) && rootMotionAxes.x >= 0 && rootMotionAxes.x <= 1 &&
        std::isfinite(rootMotionAxes.y) && rootMotionAxes.y >= 0 && rootMotionAxes.y <= 1 &&
        std::isfinite(rootMotionAxes.z) && rootMotionAxes.z >= 0 && rootMotionAxes.z <= 1 &&
        std::isfinite(playbackSpeed) && std::abs(playbackSpeed) <= 1e6f &&
        std::isfinite(blendSpeed) && std::abs(blendSpeed) <= 1e6f &&
        std::isfinite(elapsed) && elapsed >= 0 && elapsed <= 1e12f &&
        std::isfinite(speedCurveDuration) && speedCurveDuration >= 0 && speedCurveDuration <= 1e12f &&
        std::isfinite(blendWeight) && blendWeight >= 0 && blendWeight <= 1 &&
        std::isfinite(crossfadeDuration) && crossfadeDuration >= 0 && crossfadeDuration <= 1e12f &&
        std::isfinite(blendElapsed) && blendElapsed >= 0 && blendElapsed <= 1e12f &&
        std::isfinite(transitionElapsed) && transitionElapsed >= 0 && transitionElapsed <= 1e12f;
}
void SkeletalAnimationComponent::Play() {
    playing = true;
}
void SkeletalAnimationComponent::Pause() {
    playing = false;
}
void SkeletalAnimationComponent::Stop() {
    playing = false;
    elapsed = blendElapsed = transitionElapsed = 0;
    transitioning = false;
    playback.Stop();
    controller.Stop();
    blendPlayback.Stop();
    samples.clear();
    rootRanges.clear();
    frozenSource.clear();
    for(auto& layer : layers) {
        layer.elapsed = 0;
        layer.clock.Stop();
    }
}
void SkeletalAnimationComponent::Seek(float seconds) {
    if(!std::isfinite(seconds) || seconds < 0 || seconds > 1e12f) {
        return;
    }
    const float delta = seconds - elapsed;
    controller.Seek(seconds);
    for(auto& layer : layers) {
        layer.elapsed = (std::max)(0.0f, layer.elapsed + delta);
    }
    elapsed = seconds;
    blendElapsed = transitioning ? (std::max)(0.0f, blendElapsed + delta) : seconds;
    if(transitioning) {
        transitionElapsed = (std::max)(0.0f, transitionElapsed + delta);
    }
}
void SkeletalAnimationComponent::CrossFade(const std::string& target, float seconds, float targetSpeed, bool targetLoop) {
    if(target.empty() || !std::isfinite(seconds) || seconds < 0 || !std::isfinite(targetSpeed) || std::abs(targetSpeed) > 1e6f) {
        return;
    }
    // 直前frameで評価した入力の時刻/比率を固定する。支配clipへの切替によるjumpを作らない。
    if((transitioning || blendWeight > 0) && !samples.empty()) {
        frozenSource.clear();
        for(const auto& sample : samples) {
            if(sample.weight > 0 && sample.mode == PoseBlendMode::Normalized) {
                frozenSource.push_back(sample);
            }
        }
    } else {
        frozenSource.clear();
    }
    blendClipAsset = target;
    blendSpeed = targetSpeed;
    blendLoop = targetLoop;
    blendElapsed = transitionElapsed = 0;
    blendPlayback.Stop();
    crossfadeDuration = seconds;
    blendWeight = seconds == 0 ? 1.0f : 0.0f;
    transitioning = true;
    playing = true;
}
}
