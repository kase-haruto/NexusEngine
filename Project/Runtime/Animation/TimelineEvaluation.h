#pragma once
#include "AnimationDocument.h"
#include "AnimationControllerPlayer.h"
#include <span>

namespace NexusEngine {
struct TimelineTraversal {
    double begin = 0, end = 0;
    uint64_t repeats = 1;
};
/** \brief loopのprefix/全cycle/tailへ分割し、大きいdtでも無制限の反復を作らない。 */
std::vector<TimelineTraversal> TraverseTimeline(double begin, double step, double duration, bool loop);
float TimelineClipWeight(const TimelineClip& clip, double time);
std::vector<ControllerEventRange> TimelineClipRanges(const TimelineClip& clip, std::span<const TimelineTraversal> traversal);
}
