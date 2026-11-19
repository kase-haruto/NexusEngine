#include "KeyframeAnimationComponent.h"
#include <cmath>

namespace NexusEngine {
bool KeyframeAnimationComponent::IsValid() const {
    return std::isfinite(playbackSpeed) && std::abs(playbackSpeed) <= 1e6f &&
        std::isfinite(elapsed) && elapsed >= 0 && elapsed <= 1e12f &&
        std::isfinite(speedCurveDuration) && speedCurveDuration >= 0 && speedCurveDuration <= 1e12f;
}
void KeyframeAnimationComponent::Play() {
    playing = true;
}
void KeyframeAnimationComponent::Pause() {
    playing = false;
}
void KeyframeAnimationComponent::Stop() {
    playing = false;
    elapsed = 0;
    playback.Stop();
    controller.Stop();
}
void KeyframeAnimationComponent::Seek(float seconds) {
    if(std::isfinite(seconds) && seconds >= 0 && seconds <= 1e12f) {
        elapsed = seconds;
        controller.Seek(seconds);
    }
}
}
