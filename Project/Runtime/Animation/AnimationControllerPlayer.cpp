#include "AnimationControllerPlayer.h"
#include "ThirdParty/nlohmann/json.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace NexusEngine {
namespace {
Error ControllerError(const std::string& message) {
    return Error(ErrorCategory::Scene, 397, "Animation controller: " + message);
}
}
float AnimationControllerPlayer::Parameter(const std::string& name) const {
    const auto found = std::ranges::find(parameters_, name, &std::pair<std::string, float>::first);
    return found == parameters_.end() ? 0 : found->second;
}
void AnimationControllerPlayer::SetFloat(const std::string& name, float value) {
    if(std::isfinite(value) && !name.empty()) {
        const auto found = std::ranges::find(parameters_, name, &std::pair<std::string, float>::first);
        if(found != parameters_.end()) {
            found->second = value;
        } else {
            parameters_.emplace_back(name, value);
        }
    }
}
void AnimationControllerPlayer::SetBool(const std::string& name, bool value) {
    SetFloat(name, value ? 1.0f : 0.0f);
}
void AnimationControllerPlayer::SetTrigger(const std::string& name) {
    SetBool(name, true);
}
Result<void> AnimationControllerPlayer::SetParameters(const std::string& text) {
    if(text == lastParameters_) {
        return {};
    }
    try {
        if(text.size() > 1024 * 1024) {
            return std::unexpected(ControllerError("parameter input too large"));
        }
        const auto json = nlohmann::json::parse(text);
        if(!json.is_object() || json.size() > 4096) {
            return std::unexpected(ControllerError("parameters must be an object"));
        }
        std::vector<std::pair<std::string, float>> values;
        for(const auto& [name, entry] : json.items()) {
            if(!entry.is_boolean() && !entry.is_number()) {
                return std::unexpected(ControllerError("parameter must be numeric or boolean"));
            }
            const float value = entry.is_boolean() ? (entry.get<bool>() ? 1.0f : 0.0f) : entry.get<float>();
            if(!std::isfinite(value)) {
                return std::unexpected(ControllerError("parameter must be finite"));
            }
            values.emplace_back(name, value);
        }
        parameters_ = std::move(values);
        lastParameters_ = text;
        return {};
    } catch(const std::exception& error) {
        return std::unexpected(ControllerError(error.what()));
    }
}
std::string AnimationControllerPlayer::Parameters() {
    nlohmann::json values = nlohmann::json::object();
    for(const auto& [name, value] : parameters_) {
        values[name] = value;
    }
    lastParameters_ = values.dump();
    return lastParameters_;
}
std::vector<float> AnimationControllerPlayer::Weights(const AnimatorState& state) const {
    std::vector<float> weights(state.motions.size(), 0);
    if(weights.empty()) {
        return weights;
    }
    const float x = Parameter(state.parameterX), y = Parameter(state.parameterY);
    if(state.blend == "1D") {
        std::vector<size_t> order(weights.size());
        std::iota(order.begin(), order.end(), size_t{0});
        std::ranges::sort(order, [&](size_t first, size_t second) {
            return state.motions[first].x < state.motions[second].x;
        });
        if(x <= state.motions[order.front()].x) {
            weights[order.front()] = 1;
        } else if(x >= state.motions[order.back()].x) {
            weights[order.back()] = 1;
        } else {
            for(size_t index = 1; index < order.size(); ++index) {
                const auto first = order[index - 1], second = order[index];
                if(x <= state.motions[second].x) {
                    const float fraction = (x - state.motions[first].x) / (state.motions[second].x - state.motions[first].x);
                    weights[first] = 1 - fraction;
                    weights[second] = fraction;
                    break;
                }
            }
        }
    } else {
        double sum = 0;
        std::vector<double> raw(weights.size());
        for(size_t index = 0; index < weights.size(); ++index) {
            const auto& motion = state.motions[index];
            const double dx = static_cast<double>(x) - motion.x, dy = static_cast<double>(y) - motion.y;
            const double distance = dx * dx + dy * dy;
            if(distance < 1e-12) {
                std::ranges::fill(weights, 0.0f);
                weights[index] = 1;
                return weights;
            }
            raw[index] = 1 / distance;
            sum += raw[index];
        }
        for(size_t index = 0; index < weights.size(); ++index) {
            weights[index] = static_cast<float>(raw[index] / sum);
        }
    }
    return weights;
}
Result<std::vector<AnimationPoseSample>> AnimationControllerPlayer::Motion(const AnimatorState& state, double elapsed,
    double speed, const FloatCurve* curve, double curveDuration, const ClipResolver& resolve) const {
    std::vector<AnimationPoseSample> samples;
    const auto append = [&](const std::string& asset, double multiplier, float weight) -> Result<void> {
        auto clip = resolve(asset);
        if(!clip) {
            return std::unexpected(clip.error());
        }
        AnimationPlaybackSettings settings;
        settings.duration = (*clip)->duration;
        settings.baseSpeed = speed * state.speed * multiplier;
        if(!std::isfinite(settings.baseSpeed) || std::abs(settings.baseSpeed) > 1e6) {
            return std::unexpected(ControllerError("combined motion speed exceeds playback limits"));
        }
        settings.curveDuration = curveDuration;
        settings.initialTime = settings.baseSpeed < 0 ? settings.duration : 0;
        settings.loop = state.loop;
        AnimationPlayback clock;
        clock.Seek(elapsed, settings, curve);
        samples.push_back({asset, 0, static_cast<float>(clock.Time()), weight, false});
        samples.back().unwrappedTime = clock.UnwrappedTime();
        samples.back().motionRate = settings.baseSpeed;
        return {};
    };
    if(state.blend == "Clip") {
        auto result = append(state.asset, 1, 1);
        if(!result) {
            return std::unexpected(result.error());
        }
    } else {
        const auto weights = Weights(state);
        for(size_t index = 0; index < weights.size(); ++index) {
            if(weights[index] > 0) {
                auto result = append(state.motions[index].asset, state.motions[index].speed, weights[index]);
                if(!result) {
                    return std::unexpected(result.error());
                }
            }
        }
    }
    return samples;
}
Result<void> AnimationControllerPlayer::Update(const AnimatorDocument& document, double deltaTime, bool playing,
    double speed, const FloatCurve* curve, double curveDuration, const ClipResolver& resolve) {
    auto valid = ValidateAnimationDocument(document);
    if(!valid) {
        return valid;
    }
    ranges_.clear();
    motionRanges_.clear();
    const auto record = [&](const AnimationPoseSample& before, const AnimationPoseSample& after,
        double beginElapsed, double endElapsed, bool loop, float weight) {
        if(weight <= 0 || endElapsed <= beginElapsed) {
            return;
        }
        motionRanges_.push_back({after.asset, before.unwrappedTime, after.unwrappedTime, loop, weight});
        const auto clip = resolve(after.asset);
        if(!clip) {
            return;
        }
        AnimationPlaybackSettings settings;
        settings.duration = (*clip)->duration;
        settings.baseSpeed = after.motionRate;
        settings.initialTime = settings.baseSpeed < 0 ? settings.duration : 0;
        settings.curveDuration = curveDuration;
        settings.loop = loop;
        for(const auto& [begin, end] : AnimationTraversal(beginElapsed, endElapsed, settings, curve)) {
            ranges_.push_back({after.asset, begin, end, loop, weight});
        }
    };
    for(const auto& parameter : document.parameters) {
        if(std::ranges::find(parameters_, parameter.name, &std::pair<std::string, float>::first) == parameters_.end()) {
            parameters_.emplace_back(parameter.name, parameter.value);
        }
    }
    const auto state = [&](const std::string& name) -> const AnimatorState* {
        const auto found = std::ranges::find(document.states, name, &AnimatorState::name);
        return found == document.states.end() ? nullptr : &*found;
    };
    if(state_.empty()) {
        state_ = document.entry;
    } else if(!state(state_)) {
        Stop();
        state_ = document.entry;
    }
    const auto* current = state(state_);
    if(!current || !std::isfinite(speed) || std::abs(speed) > 1e6 || !std::isfinite(curveDuration) || curveDuration < 0) {
        return std::unexpected(ControllerError("invalid state/playback settings"));
    }
    const double step = playing && std::isfinite(deltaTime) && deltaTime > 0 ? deltaTime : 0;
    const double previousElapsed = elapsed_, previousNext = nextElapsed_;
    double destinationBegin = previousNext;
    elapsed_ = (std::min)(1e12, elapsed_ + step);
    auto before = Motion(*current, previousElapsed, speed, curve, curveDuration, resolve);
    auto source = Motion(*current, elapsed_, speed, curve, curveDuration, resolve);
    if(!before || !source) {
        return std::unexpected(!before ? before.error() : source.error());
    }
    if(playing) {
        for(const auto& transition : document.transitions) {
            if((transition.from != state_ && transition.from != "Any State") ||
                (transition.to == state_ && next_.empty()) || transition.to == next_) {
                continue;
            }
            bool matches = true;
            for(const auto& condition : transition.conditions) {
                const float value = Parameter(condition.parameter);
                matches &= condition.operation == ">" ? value > condition.value : condition.operation == "<" ? value < condition.value :
                    condition.operation == "==" ? value == condition.value : condition.operation == "!=" ? value != condition.value :
                    condition.operation == "True" ? value != 0 : value == 0;
            }
            if(!matches) {
                continue;
            }
            if(transition.hasExitTime) {
                auto reference = resolve(source->front().asset);
                if(!reference) {
                    return std::unexpected(reference.error());
                }
                if(std::abs(source->front().unwrappedTime) < (*reference)->duration * transition.exitTime) {
                    continue;
                }
            }
            // Any Stateを含め、割込は直前に公開した混合入力を固定する。
            if(!next_.empty() && !samples_.empty()) {
                frozen_ = samples_;
            }
            next_ = transition.to;
            nextElapsed_ = transition.offset;
            destinationBegin = transition.offset;
            fadeElapsed_ = 0;
            fadeDuration_ = transition.duration;
            for(const auto& parameter : document.parameters) {
                if(parameter.type == "Trigger" && std::ranges::any_of(transition.conditions, [&](const auto& condition) {
                    return condition.parameter == parameter.name;
                })) {
                    SetFloat(parameter.name, 0);
                }
            }
            break;
        }
    }
    auto result = frozen_.empty() ? *source : frozen_;
    bool emitSource = frozen_.empty();
    if(!next_.empty()) {
        const auto* destination = state(next_);
        if(!destination) {
            return std::unexpected(ControllerError("transition target was removed"));
        }
        nextElapsed_ = (std::min)(1e12, nextElapsed_ + step);
        fadeElapsed_ += step;
        const float weight = fadeDuration_ > 0 ? static_cast<float>((std::min)(1.0, fadeElapsed_ / fadeDuration_)) : 1;
        auto target = Motion(*destination, nextElapsed_, speed, curve, curveDuration, resolve);
        if(!target) {
            return std::unexpected(target.error());
        }
        for(auto& sample : result) {
            sample.weight *= 1 - weight;
        }
        for(auto sample : *target) {
            sample.weight *= weight;
            result.push_back(std::move(sample));
        }
        if(step > 0 && weight > 0) {
            auto begin = Motion(*destination, destinationBegin, speed, curve, curveDuration, resolve);
            if(begin) {
                for(size_t index = 0; index < target->size() && index < begin->size(); ++index) {
                    record((*begin)[index], (*target)[index], destinationBegin, nextElapsed_, destination->loop, (*target)[index].weight * weight);
                }
            }
        }
        if(weight >= 1) {
            emitSource = false;
            state_ = next_;
            elapsed_ = nextElapsed_;
            next_.clear();
            frozen_.clear();
        }
    }
    if(step > 0 && emitSource) {
        for(size_t index = 0; index < source->size() && index < before->size(); ++index) {
            float visibleWeight = (*source)[index].weight;
            if(!next_.empty()) {
                visibleWeight *= fadeDuration_ > 0 ? static_cast<float>((std::max)(0.0, 1 - fadeElapsed_ / fadeDuration_)) : 0;
            }
            record((*before)[index], (*source)[index], previousElapsed, previousElapsed + step, current->loop, visibleWeight);
        }
    }
    samples_ = std::move(result);
    return {};
}
void AnimationControllerPlayer::Stop() {
    state_.clear();
    next_.clear();
    elapsed_ = nextElapsed_ = fadeElapsed_ = fadeDuration_ = 0;
    samples_.clear();
    frozen_.clear();
    ranges_.clear();
    motionRanges_.clear();
}
void AnimationControllerPlayer::Seek(double elapsed) {
    if(std::isfinite(elapsed) && elapsed >= 0 && elapsed <= 1e12) {
        const double difference = elapsed - elapsed_;
        elapsed_ = elapsed;
        nextElapsed_ = (std::max)(0.0, nextElapsed_ + difference);
        fadeElapsed_ = (std::max)(0.0, fadeElapsed_ + difference);
    }
}
const std::vector<AnimationPoseSample>& AnimationControllerPlayer::Samples() const {
    return samples_;
}
const std::vector<ControllerEventRange>& AnimationControllerPlayer::EventRanges() const {
    return ranges_;
}
const std::vector<ControllerEventRange>& AnimationControllerPlayer::MotionRanges() const {
    return motionRanges_;
}
const std::string& AnimationControllerPlayer::State() const {
    return state_;
}
const std::string& AnimationControllerPlayer::NextState() const {
    return next_;
}
double AnimationControllerPlayer::Elapsed() const {
    return elapsed_;
}
}
