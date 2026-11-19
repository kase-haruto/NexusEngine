#include "AnimationRuntime.h"
#include "AnimationComponentEvaluator.h"
#include "TimelineEvaluation.h"
#include "Runtime/Scene/Components/AnimatorComponent.h"
#include "Runtime/Scene/Components/TimelinePlayerComponent.h"
#include "Runtime/Scene/Components/AnimationPlayerComponent.h"
#include "Runtime/Scene/Components/HierarchyComponent.h"
#include "Runtime/Scene/Components/PrimitiveRenderComponent.h"
#include "Runtime/Scene/Components/KeyframeAnimationComponent.h"
#include "Runtime/Scene/Components/CurveBindingComponent.h"
#include "Runtime/Scene/Components/SkeletalAnimationComponent.h"
#include "Graphics/Model/PoseMath.h"
#include "RetargetProfile.h"
#include "Runtime/Scene/Components/TransformComponent.h"
#include "Platform/FileSystem/AssetIdentity.h"
#include "ThirdParty/nlohmann/json.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace NexusEngine {
namespace {
using Json = nlohmann::json;
Error RuntimeError(const std::string& text) {
    return Error(ErrorCategory::Scene, 391, "Animation: " + text);
}
std::map<std::string, float> Parameters(const std::string& text) {
    std::map<std::string, float> result;
    try {
        const auto json = Json::parse(text);
        for(const auto& [name, value] : json.items()) {
            if(value.is_boolean()) {
                result[name] = value.get<bool>() ? 1.0f : 0.0f;
            } else if(value.is_number()) {
                const float number = value.get<float>();
                if(std::isfinite(number)) {
                    result[name] = number;
                }
            }
        }
    } catch(const std::exception&) {
        // 不正な外部入力はパラメーターを実行しない。アセット定義の既定値を使う。
    }
    return result;
}
}
void AnimationRuntime::SetRoot(std::filesystem::path root) {
    if(root_ != root) {
        Invalidate();
        root_ = std::move(root);
        clips_.SetRoot(root_);
        curves_.SetRoot(root_);
        layers_.SetRoot(root_);
        poses_.SetRoot(root_);
    }
}
void AnimationRuntime::ResetScene() noexcept {
    properties_.Reset();
    previousTimes_.clear();
    scene_ = nullptr;
    events_.clear();
}
void AnimationRuntime::OverrideClip(const std::string& reference, const AnimationClipDocument& clip) {
    clips_.Override(reference, clip);
}
void AnimationRuntime::OverrideController(const std::string& reference, const AnimatorDocument& controller) {
    controllers_[reference] = controller;
}
void AnimationRuntime::OverrideTimeline(const std::string& reference, const TimelineDocument& timeline) {
    timelines_[reference] = timeline;
}
const std::filesystem::path& AnimationRuntime::Root() const noexcept {
    return root_;
}
void AnimationRuntime::SetEnabled(bool enabled) {
    enabled_ = enabled;
}
void AnimationRuntime::Invalidate() {
    clips_.Invalidate();
    curves_.Invalidate();
    layers_.Invalidate();
    poses_.Invalidate();
    controllers_.clear();
    timelines_.clear();
}
const std::vector<AnimationRuntime::Event>& AnimationRuntime::Events() const noexcept {
    return events_;
}
void AnimationRuntime::SetEventHandler(std::function<void(const Event&)> handler) {
    handler_ = std::move(handler);
}
Result<const AnimationClipDocument*> AnimationRuntime::Clip(const std::string& reference) {
    // glTFのsubassetには汎用Property trackはない。姿勢は描画アセット層で評価する。
    if(reference.find("#animation/") != std::string::npos) {
        static const AnimationClipDocument empty;
        return &empty;
    }
    auto loaded = clips_.Load(reference);
    if(!loaded) {
        return std::unexpected(loaded.error());
    }
    return loaded->get();
}
CurveAssets& AnimationRuntime::Curves() noexcept {
    return curves_;
}
const std::vector<std::string>& AnimationRuntime::Diagnostics() const noexcept {
    return properties_.Diagnostics();
}
Result<std::vector<std::string>> AnimationRuntime::SkeletonJoints(const std::string& model) {
    return poses_.ModelJoints(model);
}
Result<const AnimatorDocument*> AnimationRuntime::Controller(const std::string& reference) {
    if(const auto found = controllers_.find(reference); found != controllers_.end()) {
        return &found->second;
    }
    auto path = ResolveAssetReference(root_, reference);
    if(!path) {
        return std::unexpected(path.error());
    }
    auto loaded = LoadAnimatorDocument(*path);
    if(!loaded) {
        return std::unexpected(loaded.error());
    }
    return &controllers_.emplace(reference, std::move(*loaded)).first->second;
}
Result<const TimelineDocument*> AnimationRuntime::Timeline(const std::string& reference) {
    if(const auto found = timelines_.find(reference); found != timelines_.end()) {
        return &found->second;
    }
    auto path = ResolveAssetReference(root_, reference);
    if(!path) {
        return std::unexpected(path.error());
    }
    auto loaded = LoadTimelineDocument(*path);
    if(!loaded) {
        return std::unexpected(loaded.error());
    }
    return &timelines_.emplace(reference, std::move(*loaded)).first->second;
}
Entity AnimationRuntime::Target(Entity root, const std::string& path) const {
    return properties_.Target(root, path);
}
void AnimationRuntime::InitializeTypes(const ReflectionRegistration& reflection) {
    properties_.Initialize(reflection);
}
void AnimationRuntime::RestoreProperties() {
    properties_.Restore();
}
Result<void> AnimationRuntime::Apply(Entity entity, const std::vector<AnimationPoseSample>& samples) {
    return properties_.Apply(entity, samples, [this](const auto& reference) { return Clip(reference); });
}
Result<void> AnimationRuntime::Sample(Scene& scene, Entity entity, const std::vector<AnimationPoseSample>& samples,
    const ReflectionRegistration& reflection) {
    if(scene_ != &scene) {
        properties_.Reset();
        scene_ = &scene;
    }
    InitializeTypes(reflection);
    RestoreProperties();
    return Apply(entity, samples);
}
std::vector<AnimationPoseSample> AnimationRuntime::StateSamples(const AnimatorState& state, float time, const std::map<std::string, float>& parameters) {
    if(state.blend == "Clip") {
        return {{state.asset, 0, time * state.speed, 1, state.loop}};
    }
    const auto parameter = [&](const std::string& name) {
        const auto found = parameters.find(name);
        return found == parameters.end() ? 0.0f : found->second;
    };
    const float x = parameter(state.parameterX), y = parameter(state.parameterY);
    std::vector<float> weights(state.motions.size());
    if(state.blend == "1D") {
        std::vector<size_t> order(state.motions.size());
        for(size_t index = 0; index < order.size(); ++index) {
            order[index] = index;
        }
        std::ranges::sort(order, [&](size_t a, size_t b) { return state.motions[a].x < state.motions[b].x; });
        if(x <= state.motions[order.front()].x) {
            weights[order.front()] = 1;
        } else if(x >= state.motions[order.back()].x) {
            weights[order.back()] = 1;
        } else {
            for(size_t index = 1; index < order.size(); ++index) {
                const auto left = order[index - 1], right = order[index];
                if(x <= state.motions[right].x) {
                    const float amount = (x - state.motions[left].x) / (state.motions[right].x - state.motions[left].x);
                    weights[left] = 1 - amount;
                    weights[right] = amount;
                    break;
                }
            }
        }
    } else {
        // 2Dは全点の逆距離重み。特定の方向への飛び移りを避け、点の上ではそのclipだけを使う。
        float total = 0;
        for(size_t index = 0; index < weights.size(); ++index) {
            const auto& motion = state.motions[index];
            const float distance = (x-motion.x)*(x-motion.x) + (y-motion.y)*(y-motion.y);
            if(distance < 1e-8f) {
                std::fill(weights.begin(), weights.end(), 0.0f);
                weights[index] = 1;
                total = 1;
                break;
            }
            weights[index] = 1 / distance;
            total += weights[index];
        }
        for(auto& weight : weights) {
            weight /= total;
        }
    }
    std::vector<AnimationPoseSample> samples;
    for(size_t index = 0; index < weights.size(); ++index) {
        if(weights[index] > 0) {
            const auto& motion = state.motions[index];
            samples.push_back({motion.asset, 0, time * state.speed * motion.speed, weights[index], state.loop});
        }
    }
    return samples;
}
Result<void> AnimationRuntime::EvaluateAnimator(Entity entity, AnimatorComponent& animator, float deltaTime) {
    if(animator.controllerAsset.empty()) {
        return {};
    }
    auto loaded = Controller(animator.controllerAsset);
    if(!loaded) {
        return std::unexpected(loaded.error());
    }
    const auto& document = **loaded;
    auto parameters = Parameters(animator.parameters);
    for(const auto& parameter : document.parameters) {
        parameters.try_emplace(parameter.name, parameter.value);
    }
    if(animator.activeController != animator.controllerAsset) {
        animator.activeController = animator.controllerAsset;
        animator.state = document.entry;
        animator.stateTime = 0;
        animator.nextState.clear();
    }
    const auto state = [&](const std::string& name) -> const AnimatorState* {
        const auto found = std::ranges::find(document.states, name, &AnimatorState::name);
        return found == document.states.end() ? nullptr : &*found;
    };
    const auto* current = state(animator.state);
    if(!current) {
        animator.state = document.entry;
        animator.stateTime = 0;
        animator.nextState.clear();
        current = state(document.entry);
    }
    const float step = animator.playing ? deltaTime : 0;
    animator.stateTime += step;
    if(animator.nextState.empty() && animator.playing) {
        for(const auto& transition : document.transitions) {
            if((transition.from != animator.state && transition.from != "Any State") || transition.to == animator.state) {
                continue;
            }
            if(transition.hasExitTime) {
                auto clip = Clip(current->asset);
                if(!clip) {
                    return std::unexpected(clip.error());
                }
                if(animator.stateTime * current->speed < (*clip)->duration * transition.exitTime) {
                    continue;
                }
            }
            bool matches = true;
            for(const auto& condition : transition.conditions) {
                const float value = parameters[condition.parameter];
                matches &= condition.operation == ">" ? value > condition.value : condition.operation == "<" ? value < condition.value :
                    condition.operation == "==" ? value == condition.value : condition.operation == "!=" ? value != condition.value :
                    condition.operation == "True" ? value != 0 : value == 0;
            }
            if(!matches) {
                continue;
            }
            animator.nextState = transition.to;
            animator.nextTime = transition.offset;
            animator.transitionTime = 0;
            animator.transitionDuration = transition.duration;
            for(const auto& parameter : document.parameters) {
                if(parameter.type == "Trigger" && std::ranges::any_of(transition.conditions, [&](const auto& condition) { return condition.parameter == parameter.name; })) {
                    animator.ResetTrigger(parameter.name);
                    parameters[parameter.name] = 0;
                }
            }
            break;
        }
    }
    auto samples = StateSamples(*current, animator.stateTime, parameters);
    if(!animator.nextState.empty()) {
        const auto* next = state(animator.nextState);
        if(!next) {
            return std::unexpected(RuntimeError("transition target was removed"));
        }
        animator.nextTime += step;
        animator.transitionTime += step;
        const float blend = animator.transitionDuration > 0 ? (std::min)(animator.transitionTime / animator.transitionDuration, 1.0f) : 1;
        auto target = StateSamples(*next, animator.nextTime, parameters);
        for(auto& sample : samples) {
            sample.weight *= 1 - blend;
        }
        for(auto& sample : target) {
            sample.weight *= blend;
            samples.push_back(std::move(sample));
        }
        if(blend >= 1) {
            animator.state = animator.nextState;
            animator.stateTime = animator.nextTime;
            animator.nextState.clear();
        }
    }
    auto* player = entity.GetComponent<AnimationPlayerComponent>();
    if(!player) {
        player = entity.AddComponent<AnimationPlayerComponent>();
    }
    player->runtimeDriven = true;
    player->samplesAdvancing = animator.playing;
    player->samples = std::move(samples);
    return {};
}
Result<void> AnimationRuntime::EvaluateTimeline(Entity entity, TimelinePlayerComponent& timeline, float deltaTime) {
    if(timeline.timelineAsset.empty()) {
        return {};
    }
    if(!timeline.IsValid()) {
        return std::unexpected(RuntimeError("invalid Timeline playback settings"));
    }
    auto loaded = Timeline(timeline.timelineAsset);
    if(!loaded) {
        return std::unexpected(loaded.error());
    }
    const auto& document = **loaded;
    const auto traversal = TraverseTimeline(timeline.time, timeline.playing ? static_cast<double>(deltaTime) * timeline.speed : 0,
        document.duration, timeline.loop || document.loop);
    if(timeline.playing) {
        timeline.time += deltaTime * timeline.speed;
    }
    timeline.time = AnimationClipTime(timeline.time, document.duration, timeline.loop || document.loop);
    std::map<std::pair<uint32_t, uint32_t>, std::pair<Entity, std::vector<AnimationPoseSample>>> targets;
    std::map<std::pair<uint32_t, uint32_t>, std::vector<ControllerEventRange>> ranges;
    for(const auto& track : document.tracks) {
        if(track.muted) {
            continue;
        }
        auto target = Target(entity, track.target);
        if(!target.IsAlive()) {
            return std::unexpected(RuntimeError("Timeline target not found: " + track.target));
        }
        bool active = false;
        const auto targetId = std::pair{target.GetId().index, target.GetId().generation};
        if(track.type == "Animation") {
            targets[targetId].first = target;
            for(const auto& clip : track.clips) {
                const auto traversed = TimelineClipRanges(clip, traversal);
                ranges[targetId].insert(ranges[targetId].end(), traversed.begin(), traversed.end());
            }
        }
        std::vector<AnimationPoseSample> samples;
        for(const auto& clip : track.clips) {
            if(timeline.time < clip.start || (timeline.time >= clip.start + clip.duration && timeline.time != document.duration)) {
                continue;
            }
            active = true;
            const float elapsed = timeline.time - clip.start;
            float weight = 1;
            if(clip.easeIn > 0) {
                weight *= (std::min)(elapsed / clip.easeIn, 1.0f);
            }
            if(clip.easeOut > 0) {
                weight *= (std::min)((clip.duration - elapsed) / clip.easeOut, 1.0f);
            }
            samples.push_back({clip.asset, 0, clip.clipIn + elapsed * clip.speed, weight, clip.loop});
        }
        if(track.type == "Activation") {
            AnimationClipDocument activation;
            activation.tracks.push_back({"", "PrimitiveRender", "enabled", AnimationInterpolation::Step, {{0, {active ? 1.0f : 0.0f}, {}, {}}}});
            const auto key = ":activation/" + std::to_string(target.GetId().index);
            clips_.Override(key, activation);
            auto applied = Apply(target, {{key, 0, 0, 1, false}});
            if(!applied) {
                return applied;
            }
        } else if(!samples.empty()) {
            auto& item = targets[{target.GetId().index, target.GetId().generation}];
            item.first = target;
            item.second.insert(item.second.end(), samples.begin(), samples.end());
        }
    }
    for(auto& [id, item] : targets) {
        auto* keyframe = item.first.GetComponent<KeyframeAnimationComponent>();
        auto* skeletal = item.first.GetComponent<SkeletalAnimationComponent>();
        if(keyframe || skeletal) {
            // 新componentをTimelineが借用し、controllerの時計を止めて同じimmutable clipを評価する。
            if(keyframe) {
                keyframe->timelineDriven = true;
                keyframe->samples.clear();
                keyframe->timelineRanges.clear();
            }
            if(skeletal) {
                skeletal->timelineDriven = true;
                skeletal->samples.clear();
                skeletal->timelineRanges.clear();
            }
            for(const auto& sample : item.second) {
                auto clip = Clip(sample.asset);
                if(!clip) {
                    return std::unexpected(clip.error());
                }
                const auto kind = EffectiveAnimationClipKind(**clip);
                if(kind == AnimationClipKind::Keyframe && keyframe) {
                    keyframe->samples.push_back(sample);
                } else if(kind == AnimationClipKind::Skeletal && skeletal) {
                    skeletal->samples.push_back(sample);
                } else {
                    return std::unexpected(RuntimeError("Timeline clip requires the matching Keyframe/Skeletal component: " + sample.asset));
                }
            }
            std::ranges::stable_sort(ranges[id], [](const auto& first, const auto& second) {
                return std::tie(first.segment, first.timelineBegin) < std::tie(second.segment, second.timelineBegin);
            });
            for(const auto& range : ranges[id]) {
                const auto clip = Clip(range.asset);
                if(clip && EffectiveAnimationClipKind(**clip) == AnimationClipKind::Keyframe && keyframe) {
                    keyframe->timelineRanges.push_back(range);
                } else if(clip && EffectiveAnimationClipKind(**clip) == AnimationClipKind::Skeletal && skeletal) {
                    skeletal->timelineRanges.push_back(range);
                }
            }
            continue;
        }
        if(item.second.empty()) {
            // 旧Animator/AnimationPlayerでは空白区間に制御を返す既存契約を維持する。
            // typed playerは上の分岐で明示的に借用し、bind/originalへ戻している。
            continue;
        }
        auto* player = item.first.GetComponent<AnimationPlayerComponent>();
        if(!player) {
            player = item.first.AddComponent<AnimationPlayerComponent>();
        }
        player->runtimeDriven = true;
        player->samplesAdvancing = timeline.playing;
        player->samples = std::move(item.second);
    }
    return {};
}
void AnimationRuntime::CollectEvents(Entity entity, const std::vector<AnimationPoseSample>& samples, float deltaTime) {
    std::set<std::pair<std::string, size_t>> dispatched;
    for(const auto& sample : samples) {
        if(sample.weight <= 0) {
            continue;
        }
        auto loaded = Clip(sample.asset);
        if(!loaded || (*loaded)->duration <= 0) {
            continue;
        }
        const auto& document = **loaded;
        const float end = sample.time;
        const auto key = std::make_tuple(entity.GetId().index, entity.GetId().generation, sample.asset);
        const auto previous = previousTimes_.find(key);
        const float begin = previous == previousTimes_.end() ? (std::max)(0.0f, end - deltaTime) : previous->second;
        previousTimes_[key] = end;
        if(deltaTime <= 0 || end <= begin) {
            continue;
        }
        for(size_t index = 0; index < document.events.size(); ++index) {
            const auto& event = document.events[index];
            const auto cycle = std::floor((end - event.time) / document.duration);
            const float occurrence = sample.loop ? event.time + cycle * document.duration : event.time;
            const bool initialZero = previous == previousTimes_.end() && occurrence == 0 && begin == 0;
            if(occurrence >= 0 && (occurrence > begin || initialZero) && occurrence <= end && dispatched.emplace(sample.asset, index).second) {
                events_.push_back({entity, sample.asset, event.name, event.payload});
            }
        }
    }
}
Result<void> AnimationRuntime::Update(Scene& scene, float deltaTime, const ReflectionRegistration& reflection) {
    if(scene_ != &scene) {
        properties_.Reset();
        scene_ = &scene;
    }
    if(root_.empty() || !enabled_) {
        RestoreProperties();
        scene.ForEach<AnimationPlayerComponent>([deltaTime](Entity, auto& player) {
            player.Advance(deltaTime);
        });
        return {};
    }
    InitializeTypes(reflection);
    RestoreProperties();
    events_.clear();
    std::vector<Entity> animators, timelines, players;
    scene.ForEach<KeyframeAnimationComponent>([](Entity, auto& component) {
        component.timelineDriven = false;
    });
    scene.ForEach<SkeletalAnimationComponent>([](Entity, auto& component) {
        component.timelineDriven = false;
    });
    scene.ForEach<AnimationPlayerComponent>([](Entity, auto& player) { player.samples.clear(); });
    scene.ForEach<AnimatorComponent>([&](Entity entity, const auto&) { animators.push_back(entity); });
    scene.ForEach<TimelinePlayerComponent>([&](Entity entity, const auto&) { timelines.push_back(entity); });
    // 構造変更はstorage反復の外で行う。Timelineを後に評価し、同じ対象への優先権を明確にする。
    for(auto entity : animators) {
        auto result = EvaluateAnimator(entity, *entity.GetComponent<AnimatorComponent>(), deltaTime);
        if(!result) {
            return result;
        }
    }
    for(auto entity : timelines) {
        auto result = EvaluateTimeline(entity, *entity.GetComponent<TimelinePlayerComponent>(), deltaTime);
        if(!result) {
            return result;
        }
    }
    scene.ForEach<AnimationPlayerComponent>([&](Entity entity, const auto&) { players.push_back(entity); });
    for(auto entity : players) {
        auto& player = *entity.GetComponent<AnimationPlayerComponent>();
        float eventDelta = player.samplesAdvancing ? deltaTime : 0;
        if(player.samples.empty() && !player.runtimeDriven) {
            player.Advance(deltaTime);
            if(!player.animationAsset.empty()) {
                player.samples.push_back({player.animationAsset, 0, player.time, 1-player.blendWeight, player.loop});
                if(player.blendWeight > 0) {
                    player.samples.push_back({player.blendAnimationAsset, player.blendAnimationIndex, player.blendTime, player.blendWeight, player.blendLoop});
                }
            }
            eventDelta = player.playing ? deltaTime * player.playbackSpeed : 0;
        }
        auto applied = Apply(entity, player.samples);
        if(!applied) {
            return applied;
        }
        CollectEvents(entity, player.samples, eventDelta);
    }
    // 新componentはlegacyより後に評価する。競合はbinding側の診断に残す。
    const auto control = [&](auto& component, AnimationClipKind expected) {
        if(component.timelineDriven) {
            component.runtimeDriven = true;
            return;
        }
        component.runtimeDriven = !component.controllerAsset.empty();
        if(!component.runtimeDriven) {
            component.activeController.clear();
            return;
        }
        auto document = Controller(component.controllerAsset);
        if(!document || !component.IsValid()) {
            properties_.Warn("Invalid animation controller or component settings: " + component.controllerAsset);
            component.samples = component.controller.Samples();
            return;
        }
        if(component.activeController != component.controllerAsset) {
            if(!component.activeController.empty()) {
                component.elapsed = 0;
            }
            component.controller.Stop();
            if(component.elapsed > 0) {
                component.controller.Seek(component.elapsed);
            }
            component.activeController = component.controllerAsset;
        } else if(component.elapsed != static_cast<float>(component.controller.Elapsed())) {
            component.controller.Seek(component.elapsed);
        }
        const auto parameters = component.controller.SetParameters(component.controllerParameters);
        if(!parameters) {
            properties_.Warn(parameters.error().GetMessageText());
            component.samples = component.controller.Samples();
            return;
        }
        std::shared_ptr<const FloatCurve> curve;
        if(!component.speedCurveAsset.empty()) {
            const auto loaded = curves_.Load(component.speedCurveAsset);
            if(loaded) {
                curve = *loaded;
            } else {
                properties_.Warn(loaded.error().GetMessageText());
            }
        }
        const auto evaluated = component.controller.Update(**document, deltaTime, component.playing, component.playbackSpeed,
            curve.get(), component.speedCurveDuration, [&](const auto& asset) {
                return clips_.Load(asset);
            });
        if(!evaluated) {
            properties_.Warn(evaluated.error().GetMessageText());
        }
        component.samples = component.controller.Samples();
        component.elapsed = static_cast<float>(component.controller.Elapsed());
        component.controllerParameters = component.controller.Parameters();
        for(const auto& sample : component.samples) {
            auto clip = clips_.Load(sample.asset);
            if(!clip || EffectiveAnimationClipKind(**clip) != expected) {
                properties_.Warn("Controller motion does not match the animation component kind: " + sample.asset);
                component.samples.clear();
                break;
            }
        }
    };
    scene.ForEach<KeyframeAnimationComponent>([&](Entity, auto& component) {
        control(component, AnimationClipKind::Keyframe);
    });
    scene.ForEach<SkeletalAnimationComponent>([&](Entity, auto& component) {
        component.controlledBeginElapsed = component.controller.Elapsed();
        control(component, AnimationClipKind::Skeletal);
    });
    std::set<std::tuple<uint32_t, uint32_t, std::string, size_t>> dispatched;
    EvaluateAnimationComponents(scene, deltaTime, clips_, curves_, layers_, properties_,
        [&](Entity entity, const std::string& asset, const AnimationClipDocument& clip, double begin, double end, bool loop) {
            // 既定は前進だけ。逆方向はplayerの明示設定で[end,begin)を通知する。
            const auto* keyframe = entity.GetComponent<KeyframeAnimationComponent>();
            const auto* skeletal = entity.GetComponent<SkeletalAnimationComponent>();
            const bool reverse = end < begin;
            const bool allowReverse = (keyframe && keyframe->reverseEvents) || (skeletal && skeletal->reverseEvents);
            if(end == begin || clip.duration <= 0 || (reverse && !allowReverse)) {
                return;
            }
            for(size_t index = 0; index < clip.events.size(); ++index) {
                const auto& event = clip.events[index];
                const double cycle = loop ? (reverse ? std::ceil((end - event.time) / clip.duration) : std::floor((end - event.time) / clip.duration)) : 0;
                const double occurrence = event.time + cycle * clip.duration;
                const bool crossed = reverse ? occurrence < begin && occurrence >= end : occurrence > begin && occurrence <= end;
                if(crossed &&
                    dispatched.emplace(entity.GetId().index, entity.GetId().generation, asset, index).second) {
                    // 複数loopを飛び越した同一eventは最新1回へまとめ、無制限のcallbackを避ける。
                    events_.push_back({entity, asset, event.name, event.payload, reverse});
                }
            }
        });
    scene.ForEach<CurveBindingComponent>([&](Entity entity, auto& binding) {
        if(binding.curveAsset.empty()) {
            return;
        }
        if(!binding.IsValid()) {
            properties_.Warn("Invalid Curve binding settings: " + binding.curveAsset);
            return;
        }
        const auto curve = curves_.Load(binding.curveAsset);
        if(!curve) {
            properties_.Warn(curve.error().GetMessageText());
            return;
        }
        if(binding.playing && !binding.manual) {
            // Curve出力の意味はbinding側に置く。負速度で先頭を越えたloopも終端へ戻す。
            double next = binding.elapsed + static_cast<double>(deltaTime) * binding.playbackSpeed;
            if(binding.loop && next < 0) {
                next = std::fmod(next, binding.duration) + binding.duration;
            }
            binding.elapsed = static_cast<float>(std::clamp(next, 0.0, 1e12));
        }
        double input = binding.manual ? binding.input : static_cast<double>(binding.elapsed) / binding.duration;
        if(!binding.manual && binding.loop) {
            input -= std::floor(input);
        }
        const double value = (*curve)->Evaluate(input) * binding.outputScale + binding.outputBias;
        const auto applied = properties_.ApplyValue(entity, binding.target, binding.targetComponent,
            binding.targetProperty, binding.channel, static_cast<float>(value));
        if(!applied) {
            properties_.Warn(applied.error().GetMessageText());
        }
    });
    ApplyRootMotion(scene);
    // callbackからSceneへ変更する場合も、storage反復と姿勢評価が終了してから通知する。
    if(handler_) {
        const auto events = events_;
        for(const auto& event : events) {
            handler_(event);
        }
    }
    return {};
}
void AnimationRuntime::ApplyRootMotion(Scene& scene) {
    scene.ForEach<SkeletalAnimationComponent>([&](Entity entity, auto& component) {
        component.extractedRootMotion = {};
        if(!component.rootMotion || !component.IsValid() || (component.samples.empty() && component.rootRanges.empty())) {
            return;
        }
        auto* transform = entity.GetComponent<TransformComponent>();
        const auto* model = entity.GetComponent<PrimitiveRenderComponent>();
        if(!transform || !model || model->meshAsset.empty() || component.rootMotionJoint.empty()) {
            properties_.Warn("Root Motion requires a Transform, model and explicit root joint");
            return;
        }
        auto target = poses_.Load(component.samples.empty() ? component.rootRanges.front().asset : component.samples.front().asset, model->meshAsset, clips_);
        if(!target) {
            properties_.Warn(target.error().GetMessageText());
            return;
        }
        const auto paths = SkeletonNodePaths(**target);
        const auto root = std::ranges::find(paths, component.rootMotionJoint);
        if(root == paths.end() || std::ranges::count(paths, component.rootMotionJoint) != 1) {
            properties_.Warn("Root Motion joint is missing or ambiguous");
            return;
        }
        const auto rootIndex = static_cast<uint32_t>(root - paths.begin());
        for(auto& sample : component.samples) {
            sample.rootMotionJoint = component.rootMotionJoint;
            sample.rootMotionAxes = component.rootMotionAxes;
            sample.removeRootRotation = component.rootMotionRotation;
        }
        RootMotionDelta result;
        float accumulated = 0;
        for(const auto& range : component.rootRanges) {
            if(range.mode == PoseBlendMode::Normalized) {
                accumulated += range.weight;
            }
        }
        const float normalizedTotal = (std::max)(1.0f, accumulated);
        accumulated = (std::max)(0.0f, 1 - accumulated);
        for(const auto& range : component.rootRanges) {
            if(range.weight <= 0 || (!range.mask.empty() && !std::ranges::any_of(range.mask, [&](const auto& mask) {
                return component.rootMotionJoint.starts_with(mask);
            }))) {
                continue;
            }
            auto asset = poses_.Load(range.asset, model->meshAsset, clips_);
            if(!asset) {
                properties_.Warn(asset.error().GetMessageText());
                continue;
            }
            auto motion = ExtractRootMotion(**asset, rootIndex, range.begin, range.end, range.loop);
            if(!motion) {
                properties_.Warn(motion.error().GetMessageText());
                continue;
            }
            *motion = RepeatRootMotion(*motion, range.repeats);
            if(range.sequential) {
                motion->translation = motion->translation * range.weight;
                motion->rotation = BlendPoseRotation(Quaternion::Identity(), motion->rotation, range.weight);
                result = ComposeRootMotion(result, *motion);
                continue;
            }
            if(range.mode == PoseBlendMode::Normalized) {
                result.translation = result.translation + motion->translation * (range.weight / normalizedTotal);
                const float weight = range.weight / (accumulated + range.weight);
                result.rotation = BlendPoseRotation(result.rotation, motion->rotation, weight);
                accumulated += range.weight;
            } else if(range.mode == PoseBlendMode::Override) {
                result.translation = result.translation + (motion->translation - result.translation) * range.weight;
                result.rotation = BlendPoseRotation(result.rotation, motion->rotation, range.weight);
            } else {
                result.translation = result.translation + motion->translation * range.weight;
                result.rotation = Quaternion::Multiply(BlendPoseRotation(Quaternion::Identity(), motion->rotation, range.weight), result.rotation).Normalized();
            }
        }
        result.translation = {result.translation.x * component.rootMotionAxes.x, result.translation.y * component.rootMotionAxes.y,
            result.translation.z * component.rootMotionAxes.z};
        if(!component.rootMotionRotation) {
            result.rotation = Quaternion::Identity();
        }
        component.extractedRootMotion = result;
        if(component.applyRootMotion) {
            const Vector3 local{result.translation.x * transform->scale.x, result.translation.y * transform->scale.y, result.translation.z * transform->scale.z};
            const Quaternion vector{local.x, local.y, local.z, 0};
            const auto rotation = transform->rotation.Normalized();
            const Quaternion inverse{-rotation.x, -rotation.y, -rotation.z, rotation.w};
            const auto moved = Quaternion::Multiply(Quaternion::Multiply(rotation, vector), inverse);
            transform->translation = transform->translation + Vector3{moved.x, moved.y, moved.z};
            transform->rotation = Quaternion::Multiply(rotation, result.rotation).Normalized();
        }
    });
}
}
