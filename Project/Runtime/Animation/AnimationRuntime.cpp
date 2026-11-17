#include "AnimationRuntime.h"
#include "Runtime/Scene/Components/AnimatorComponent.h"
#include "Runtime/Scene/Components/TimelinePlayerComponent.h"
#include "Runtime/Scene/Components/AnimationPlayerComponent.h"
#include "Runtime/Scene/Components/HierarchyComponent.h"
#include "Runtime/Scene/Components/PrimitiveRenderComponent.h"
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
const PropertyDescriptor* Property(const TypeRegistry& types, const std::string& component, const std::string& property) {
    const auto* type = types.Find(component);
    if(!type) {
        return nullptr;
    }
    const auto found = std::ranges::find(type->properties, property, &PropertyDescriptor::id);
    return found == type->properties.end() ? nullptr : &*found;
}
std::vector<float> Numbers(const PropertyValue& value) {
    return std::visit([](const auto& item) -> std::vector<float> {
        using T = std::decay_t<decltype(item)>;
        if constexpr(std::is_same_v<T, float> || std::is_same_v<T, int> || std::is_same_v<T, bool>) {
            return {static_cast<float>(item)};
        } else if constexpr(std::is_same_v<T, std::array<float, 2>> || std::is_same_v<T, std::array<float, 3>> || std::is_same_v<T, std::array<float, 4>>) {
            return {item.begin(), item.end()};
        } else {
            return {};
        }
    }, value);
}
PropertyValue Converted(const PropertyValue& base, const std::vector<float>& values) {
    return std::visit([&](const auto& item) -> PropertyValue {
        using T = std::decay_t<decltype(item)>;
        if constexpr(std::is_same_v<T, float>) {
            return values[0];
        } else if constexpr(std::is_same_v<T, int>) {
            return static_cast<int>(std::clamp(static_cast<double>(values[0]),
                static_cast<double>((std::numeric_limits<int>::min)()), static_cast<double>((std::numeric_limits<int>::max)())));
        } else if constexpr(std::is_same_v<T, bool>) {
            return values[0] >= .5f;
        } else if constexpr(std::is_same_v<T, std::array<float, 2>> || std::is_same_v<T, std::array<float, 3>> || std::is_same_v<T, std::array<float, 4>>) {
            T output{};
            std::copy(values.begin(), values.end(), output.begin());
            return output;
        } else {
            return item;
        }
    }, base);
}
}
void AnimationRuntime::SetRoot(std::filesystem::path root) {
    if(root_ != root) {
        Invalidate();
        root_ = std::move(root);
    }
}
void AnimationRuntime::ResetScene() noexcept {
    saved_.clear();
    types_ = {};
    typesReady_ = false;
    previousTimes_.clear();
    scene_ = nullptr;
    events_.clear();
}
void AnimationRuntime::OverrideClip(const std::string& reference, const AnimationClipDocument& clip) {
    clips_[reference] = clip;
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
    clips_.clear();
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
    if(const auto found = clips_.find(reference); found != clips_.end()) {
        return &found->second;
    }
    // glTFのsubassetには汎用Property trackはない。姿勢は描画アセット層で評価する。
    if(reference.find("#animation/") != std::string::npos) {
        return &clips_.emplace(reference, AnimationClipDocument{}).first->second;
    }
    auto path = ResolveAnimationPath(root_, reference);
    if(!path) {
        return std::unexpected(path.error());
    }
    auto loaded = LoadAnimationClipDocument(*path);
    if(!loaded) {
        return std::unexpected(loaded.error());
    }
    return &clips_.emplace(reference, std::move(*loaded)).first->second;
}
Result<const AnimatorDocument*> AnimationRuntime::Controller(const std::string& reference) {
    if(const auto found = controllers_.find(reference); found != controllers_.end()) {
        return &found->second;
    }
    auto path = ResolveAnimationPath(root_, reference);
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
    auto path = ResolveAnimationPath(root_, reference);
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
    if(path.empty()) {
        return root;
    }
    auto current = root;
    size_t begin = 0;
    while(begin < path.size()) {
        const auto end = path.find('/', begin);
        const auto name = path.substr(begin, end == std::string::npos ? end : end - begin);
        Entity next;
        bool duplicate = false;
        root.GetScene()->ForEach<NameComponent, HierarchyComponent>([&](Entity candidate, const auto& label, const auto& hierarchy) {
            if(hierarchy.parent == current.GetId() && label.name == name) {
                duplicate |= next.IsAlive();
                next = candidate;
            }
        });
        if(duplicate || !next.IsAlive()) {
            return {};
        }
        current = next;
        if(end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
    return current;
}
void AnimationRuntime::InitializeTypes(const ReflectionRegistration& reflection) {
    if(!typesReady_) {
        RegisterSceneTypes(types_);
        reflection.RegisterInspector(types_);
        typesReady_ = true;
    }
}
void AnimationRuntime::RestoreProperties() {
    // 毎回未アニメーション値へ戻してから評価し、区間外や欠けたtrackで前の姿勢を残さない。
    for(const auto& saved : saved_) {
        if(saved.entity.IsAlive()) {
            const auto* property = Property(types_, saved.component, saved.property);
            if(property) {
                property->set(saved.entity, saved.value);
            }
        }
    }
    saved_.clear();
}
Result<void> AnimationRuntime::Apply(Entity entity, const std::vector<AnimationPoseSample>& samples) {
    struct Contribution {
        Entity entity;
        std::string component, property;
        std::vector<float> sum;
        float weight = 0;
    };
    std::vector<Contribution> contributions;
    for(const auto& sample : samples) {
        if(sample.asset.empty() || sample.weight <= 0) {
            continue;
        }
        auto document = Clip(sample.asset);
        if(!document) {
            return std::unexpected(document.error());
        }
        const float time = AnimationClipTime(sample.time, (*document)->duration, sample.loop);
        for(const auto& track : (*document)->tracks) {
            if(track.component == "$node") {
                continue;
            }
            auto target = Target(entity, track.target);
            const auto* property = Property(types_, track.component, track.property);
            if(!target.IsAlive() || !property) {
                return std::unexpected(RuntimeError("unresolved property binding: " + track.component + "." + track.property));
            }
            auto base = property->get(target);
            auto value = SampleAnimationTrack(track, time);
            if(base && property->kind == PropertyKind::Rotation && value.size() == 4) {
                const auto rotation = Numbers(*base);
                float dot = 0;
                for(size_t axis = 0; axis < 4; ++axis) {
                    dot += value[axis]*rotation[axis];
                }
                if(dot < 0) {
                    for(float& axis : value) {
                        axis = -axis;
                    }
                }
            }
            if(!base || Numbers(*base).size() != value.size()) {
                return std::unexpected(RuntimeError("property dimensions do not match track"));
            }
            auto found = std::find_if(contributions.begin(), contributions.end(), [&](const auto& item) {
                return item.entity == target && item.component == track.component && item.property == track.property;
            });
            if(found == contributions.end()) {
                contributions.push_back({target, track.component, track.property, std::vector<float>(value.size()), 0});
                found = contributions.end() - 1;
            }
            for(size_t index = 0; index < value.size(); ++index) {
                found->sum[index] += value[index] * sample.weight;
            }
            found->weight += sample.weight;
        }
    }
    for(auto& item : contributions) {
        const auto* property = Property(types_, item.component, item.property);
        const auto base = property->get(item.entity);
        auto original = Numbers(*base);
        const float total = (std::max)(1.0f, item.weight);
        for(size_t index = 0; index < original.size(); ++index) {
            item.sum[index] = (item.sum[index] + original[index] * (std::max)(0.0f, 1 - item.weight)) / total;
        }
        // 保存値は一度だけ記録する。TimelineがAnimatorを上書きしても復元先は元の値。
        const bool saved = std::ranges::any_of(saved_, [&](const auto& value) {
            return value.entity == item.entity && value.component == item.component && value.property == item.property;
        });
        if(!saved) {
            saved_.push_back({item.entity, item.component, item.property, *base});
        }
        if(!property->set(item.entity, Converted(*base, item.sum))) {
            return std::unexpected(RuntimeError("property rejected an animated value: " + item.property));
        }
    }
    return {};
}
Result<void> AnimationRuntime::Sample(Scene& scene, Entity entity, const std::vector<AnimationPoseSample>& samples,
    const ReflectionRegistration& reflection) {
    if(scene_ != &scene) {
        saved_.clear();
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
    if(timeline.playing) {
        timeline.time += deltaTime * timeline.speed;
    }
    timeline.time = AnimationClipTime(timeline.time, document.duration, timeline.loop || document.loop);
    std::map<std::pair<uint32_t, uint32_t>, std::pair<Entity, std::vector<AnimationPoseSample>>> targets;
    for(const auto& track : document.tracks) {
        if(track.muted) {
            continue;
        }
        auto target = Target(entity, track.target);
        if(!target.IsAlive()) {
            return std::unexpected(RuntimeError("Timeline target not found: " + track.target));
        }
        bool active = false;
        std::vector<AnimationPoseSample> samples;
        for(const auto& clip : track.clips) {
            if(timeline.time < clip.start || timeline.time >= clip.start + clip.duration) {
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
            clips_[key] = std::move(activation);
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
        (void)id;
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
        saved_.clear();
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
    // callbackからSceneへ変更する場合も、storage反復と姿勢評価が終了してから通知する。
    if(handler_) {
        const auto events = events_;
        for(const auto& event : events) {
            handler_(event);
        }
    }
    return {};
}
}
