#pragma once
#include "Foundation/Reflection/ReflectionMarkers.h"
#include <cmath>
#include <cstdint>

NEXUS_REFLECT_COMPONENT(AnimationPlayerComponent, "AnimationPlayer", "component.animationPlayer", animationIndex, playbackSpeed, loop, playing, time)

namespace NexusEngine {
// Clip indices are local to the model assigned to PrimitiveRenderComponent.
struct AnimationPlayerComponent {
    uint32_t animationIndex = 0;
    float playbackSpeed = 1.0f;
    bool loop = true;
    bool playing = true;
    float time = 0.0f; // Seconds; also allows seeking while paused.

    bool IsValid() const noexcept {
        return std::isfinite(playbackSpeed) && playbackSpeed >= 0 && std::isfinite(time) && time >= 0;
    }
    void Play(uint32_t clip = 0) noexcept { animationIndex = clip; time = 0; playing = true; }
    void Pause() noexcept { playing = false; }
    void Resume() noexcept { playing = true; }
    void Stop() noexcept { time = 0; playing = false; }
};
}
