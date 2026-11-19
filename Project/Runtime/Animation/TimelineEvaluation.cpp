#include "TimelineEvaluation.h"
#include <algorithm>
#include <cmath>

namespace NexusEngine {
std::vector<TimelineTraversal> TraverseTimeline(double begin, double step, double duration, bool loop) {
    std::vector<TimelineTraversal> traversal;
    if(!std::isfinite(begin) || !std::isfinite(step) || !std::isfinite(duration) || step <= 0 || duration <= 0) {
        return traversal;
    }
    begin = std::clamp(begin, 0.0, duration);
    if(!loop) {
        traversal.push_back({begin, (std::min)(duration, begin + step)});
        return traversal;
    }
    const double prefix = (std::min)(step, duration - begin);
    if(prefix > 0) {
        traversal.push_back({begin, begin + prefix});
    }
    step -= prefix;
    const double cycles = std::floor(step / duration);
    if(cycles > 0) {
        traversal.push_back({0, duration, static_cast<uint64_t>((std::min)(cycles, 1e12))});
        step -= cycles * duration;
    }
    if(step > 0) {
        traversal.push_back({0, step});
    }
    return traversal;
}
float TimelineClipWeight(const TimelineClip& clip, double time) {
    const double elapsed = std::clamp(time - clip.start, 0.0, static_cast<double>(clip.duration));
    const double fadeIn = clip.easeIn > 0 ? (std::min)(1.0, elapsed / clip.easeIn) : 1;
    const double fadeOut = clip.easeOut > 0 ? (std::min)(1.0, (clip.duration - elapsed) / clip.easeOut) : 1;
    return static_cast<float>(fadeIn * fadeOut);
}
std::vector<ControllerEventRange> TimelineClipRanges(const TimelineClip& clip, std::span<const TimelineTraversal> traversal) {
    std::vector<ControllerEventRange> ranges;
    for(size_t index = 0; index < traversal.size(); ++index) {
        const auto& segment = traversal[index];
        const double begin = (std::max)(segment.begin, static_cast<double>(clip.start));
        const double end = (std::min)(segment.end, static_cast<double>(clip.start) + clip.duration);
        if(end <= begin) {
            continue;
        }
        // 単一frame内でfadeを横切る場合も、区間の平均weightでRoot Motionを混合する。
        const float weight = (TimelineClipWeight(clip, begin) + 4 * TimelineClipWeight(clip, (begin + end) / 2) + TimelineClipWeight(clip, end)) / 6;
        ranges.push_back({clip.asset, clip.clipIn + (begin - clip.start) * clip.speed,
            clip.clipIn + (end - clip.start) * clip.speed, clip.loop, weight, segment.repeats, static_cast<uint32_t>(index), begin});
    }
    return ranges;
}
}
