#include "AnimationPlayback.h"
#include <algorithm>
#include <cmath>

namespace NexusEngine {
std::vector<std::pair<double, double>> AnimationTraversal(double beginElapsed, double endElapsed,
    const AnimationPlaybackSettings& settings, const FloatCurve* curve) {
    std::vector<std::pair<double, double>> ranges;
    if(!std::isfinite(beginElapsed) || !std::isfinite(endElapsed) || endElapsed <= beginElapsed) {
        return ranges;
    }
    AnimationPlayback start;
    start.Seek(beginElapsed, settings, curve);
    if(start.IsCompleted()) {
        return ranges;
    }
    const double duration = settings.curveDuration > 0 ? settings.curveDuration : settings.duration;
    std::vector<double> breaks{beginElapsed, endElapsed};
    if(curve && duration > 0 && beginElapsed < duration) {
        breaks = curve->MonotonicIntegralBreaks(beginElapsed / duration, (std::min)(1.0, endElapsed / duration));
        for(double& value : breaks) {
            value *= duration;
        }
        if(endElapsed > duration) {
            breaks.push_back(endElapsed);
        }
    }
    double position = start.UnwrappedTime();
    for(size_t index = 1; index < breaks.size(); ++index) {
        const double a = breaks[index - 1], b = breaks[index];
        const double area = curve && duration > 0 ? duration * curve->Integrate((std::min)(1.0, a / duration), (std::min)(1.0, b / duration)) +
            (std::max)(0.0, b - (std::max)(a, duration)) * curve->Evaluate(1) : b - a;
        double next = position + settings.baseSpeed * area;
        if(!settings.loop) {
            next = std::clamp(next, 0.0, settings.duration);
        }
        if(next != position) {
            ranges.emplace_back(position, next);
        }
        const bool complete = !settings.loop && ((next >= settings.duration && next > position) || (next <= 0 && next < position));
        position = next;
        if(complete) {
            break;
        }
    }
    return ranges;
}
void AnimationPlayback::Play() {
    playing_ = true;
}
void AnimationPlayback::Pause() {
    playing_ = false;
}
void AnimationPlayback::Stop() {
    playing_ = false;
    elapsed_ = time_ = unwrapped_ = 0;
    completed_ = false;
}
void AnimationPlayback::Seek(double elapsed, const AnimationPlaybackSettings& settings, const FloatCurve* curve) {
    if(!std::isfinite(elapsed) || elapsed < 0 || elapsed > 1e12 ||
        !std::isfinite(settings.duration) || settings.duration < 0 || settings.duration > 1e12 ||
        !std::isfinite(settings.curveDuration) || settings.curveDuration < 0 || settings.curveDuration > 1e12 ||
        !std::isfinite(settings.baseSpeed) || std::abs(settings.baseSpeed) > 1e6 ||
        !std::isfinite(settings.initialTime) || std::abs(settings.initialTime) > 1e12) {
        // 不正設定で時刻を汚さず停止する。利用側が診断を表示する。
        playing_ = false;
        completed_ = false;
        return;
    }
    elapsed_ = elapsed;
    const double curveDuration = settings.curveDuration > 0 ? settings.curveDuration : settings.duration;
    const auto position = [&](double seconds) {
        // 汎用curveにX>1のkeyがあっても、Speed用途では正規化区間の終端速度を保持する。
        const double area = curve && curveDuration > 0 ?
            curveDuration * curve->Integrate(0, (std::min)(1.0, seconds / curveDuration)) +
                (std::max)(0.0, seconds - curveDuration) * curve->Evaluate(1) : seconds;
        return settings.initialTime + settings.baseSpeed * area;
    };
    unwrapped_ = position(elapsed_);
    completed_ = settings.duration == 0;
    if(settings.duration == 0) {
        time_ = 0;
        return;
    }
    if(settings.loop) {
        time_ = std::fmod(unwrapped_, settings.duration);
        if(time_ < 0) {
            time_ += settings.duration;
        }
        return;
    }
    // 速度の零点とkey境界で分割し、最初のclip終端到達を探す。
    // 一度の大きなSeekで終端を越えて戻るcurveでも、小さなdt更新と結果を一致させる。
    std::vector<double> breaks{0, elapsed_};
    if(curve && curveDuration > 0) {
        breaks = curve->MonotonicIntegralBreaks(0, (std::min)(1.0, elapsed_ / curveDuration));
        for(auto& value : breaks) {
            value *= curveDuration;
        }
        if(elapsed_ > curveDuration) {
            breaks.push_back(elapsed_);
        }
    }
    for(size_t index = 1; index < breaks.size(); ++index) {
        const double first = position(breaks[index - 1]);
        const double last = position(breaks[index]);
        const bool upper = last >= settings.duration && last > first;
        const bool lower = last <= 0 && last < first;
        if(upper || lower) {
            time_ = upper ? settings.duration : 0;
            unwrapped_ = time_;
            completed_ = true;
            return;
        }
    }
    time_ = std::clamp(unwrapped_, 0.0, settings.duration);
}
void AnimationPlayback::Update(double deltaTime, const AnimationPlaybackSettings& settings, const FloatCurve* curve) {
    double next = elapsed_;
    if(playing_ && !completed_ && std::isfinite(deltaTime) && deltaTime > 0) {
        next += deltaTime;
    }
    Seek(next, settings, curve);
}
double AnimationPlayback::Elapsed() const noexcept {
    return elapsed_;
}
double AnimationPlayback::Time() const noexcept {
    return time_;
}
double AnimationPlayback::UnwrappedTime() const noexcept {
    return unwrapped_;
}
bool AnimationPlayback::IsPlaying() const noexcept {
    return playing_ && !completed_;
}
bool AnimationPlayback::IsCompleted() const noexcept {
    return completed_;
}
}
