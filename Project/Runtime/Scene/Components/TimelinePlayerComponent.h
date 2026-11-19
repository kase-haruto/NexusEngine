#pragma once
#include "Foundation/Reflection/ReflectionMarkers.h"
#include <string>

NEXUS_REFLECT_COMPONENT(TimelinePlayerComponent, "TimelinePlayer", "component.timeline", timelineAsset, time, playing, loop, speed)
NEXUS_COMPONENT_EDITOR(TimelinePlayerComponent, "Animation", "componentBrowser.description.timeline", "details.Animation")
namespace NexusEngine {
/** Timelineが対象を制御する間だけAnimatorより優先する。timeは秒単位でシーク可能。 */
struct TimelinePlayerComponent {
    std::string timelineAsset;
    float time = 0;
    bool playing = true, loop = false;
    float speed = 1;
    bool IsValid() const noexcept;
};
}
