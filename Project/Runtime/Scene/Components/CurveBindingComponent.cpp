#include "CurveBindingComponent.h"
#include <cmath>

namespace NexusEngine {
bool CurveBindingComponent::IsValid() const {
    return channel >= 0 && channel < 4 && std::isfinite(duration) && duration > 0 && duration <= 1e12f &&
        std::isfinite(elapsed) && elapsed >= 0 && elapsed <= 1e12f &&
        std::isfinite(playbackSpeed) && std::abs(playbackSpeed) <= 1e6f && std::isfinite(input) &&
        std::isfinite(outputScale) && std::isfinite(outputBias);
}
void CurveBindingComponent::Play() {
    playing = true;
}
void CurveBindingComponent::Pause() {
    playing = false;
}
void CurveBindingComponent::Stop() {
    playing = false;
    elapsed = 0;
    input = 0;
}
void CurveBindingComponent::Seek(float seconds) {
    if(std::isfinite(seconds) && seconds >= 0 && seconds <= 1e12f) {
        elapsed = seconds;
        if(manual) {
            input = seconds / duration;
        }
    }
}
}
