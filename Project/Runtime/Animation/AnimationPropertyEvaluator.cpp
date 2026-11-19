#include "AnimationPropertyEvaluator.h"
#include "Runtime/Scene/Components/HierarchyComponent.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace NexusEngine {
namespace {
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
Entity AnimationPropertyEvaluator::Target(Entity root, const std::string& path) const {
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
void AnimationPropertyEvaluator::Initialize(const ReflectionRegistration& reflection) {
    if(!typesReady_) {
        RegisterSceneTypes(types_);
        reflection.RegisterInspector(types_);
        typesReady_ = true;
    }
}
void AnimationPropertyEvaluator::Restore() {
    diagnostics_.clear();
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
Result<void> AnimationPropertyEvaluator::Apply(Entity entity, const std::vector<AnimationPoseSample>& samples, const ClipResolver& resolve, bool strict) {
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
        auto document = resolve(sample.asset);
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
            if(!target.IsAlive() || !property || (strict && !property->animatable)) {
                Warn(sample.asset + ": unresolved or disabled binding " + track.component + "." + track.property);
                continue;
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
                Warn(sample.asset + ": property dimensions do not match " + track.component + "." + track.property);
                continue;
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
        } else {
            // 評価順の後勝ちを維持する。別playerとの競合を黙って隠さない。
            Warn("Multiple animation writers: " + item.component + "." + item.property + " (later evaluator wins)");
        }
        if(!property->set(item.entity, Converted(*base, item.sum))) {
            Warn("Property rejected animated value: " + item.component + "." + item.property);
        }
    }
    return {};
}
Result<void> AnimationPropertyEvaluator::ApplyValue(Entity entity, const std::string& targetPath,
    const std::string& component, const std::string& propertyId, int channel, float value) {
    const auto target = Target(entity, targetPath);
    const auto* property = Property(types_, component, propertyId);
    if(!target.IsAlive() || !property || !property->animatable || !std::isfinite(value)) {
        Warn("Curve binding is unresolved, disabled or non-finite: " + component + "." + propertyId);
        return {};
    }
    const auto base = property->get(target);
    auto numbers = base ? Numbers(*base) : std::vector<float>{};
    // Quaternionの一軸だけの置換は回転を壊すため、通常のrotation trackへ任せる。
    if(channel < 0 || static_cast<size_t>(channel) >= numbers.size() || property->kind == PropertyKind::Rotation) {
        Warn("Curve binding channel does not match property dimensions: " + component + "." + propertyId);
        return {};
    }
    numbers[static_cast<size_t>(channel)] = value;
    AnimationClipDocument document;
    AnimationTrack track;
    track.target = targetPath;
    track.component = component;
    track.property = propertyId;
    track.keys = {{0, std::move(numbers), {}, {}}};
    document.tracks.push_back(std::move(track));
    AnimationPoseSample sample;
    sample.asset = "$curve-binding";
    sample.weight = 1;
    return Apply(entity, {sample}, [&](const auto&) -> Result<const AnimationClipDocument*> {
        return &document;
    }, true);
}
void AnimationPropertyEvaluator::Reset() {
    saved_.clear();
    diagnostics_.clear();
    types_ = {};
    typesReady_ = false;
}
const std::vector<std::string>& AnimationPropertyEvaluator::Diagnostics() const noexcept {
    return diagnostics_;
}
void AnimationPropertyEvaluator::Warn(std::string message) {
    if(diagnostics_.size() < 512) {
        diagnostics_.push_back(std::move(message));
    }
}
}
