#include "AnimationComponentEvaluator.h"
#include "Runtime/Scene/Components/KeyframeAnimationComponent.h"
#include "Runtime/Scene/Components/SkeletalAnimationComponent.h"
#include "Runtime/Scene/Components/AnimationPlayerComponent.h"
#include "Runtime/Scene/Components/PersistentIdComponent.h"
#include <algorithm>
#include <cmath>
#include <tuple>

namespace NexusEngine {
namespace {
std::shared_ptr<const FloatCurve> SpeedCurve(const std::string& reference, CurveAssets& curves, AnimationPropertyEvaluator& properties) {
    if(reference.empty()) {
        return {};
    }
    auto loaded = curves.Load(reference);
    if(!loaded) {
        properties.Warn(loaded.error().GetMessageText() + " (using speed multiplier 1)");
        return {};
    }
    return *loaded;
}
struct AdvancedRange {
    double first = 0, second = 0;
    std::vector<std::pair<double, double>> traversed;
};
std::optional<AdvancedRange> Advance(AnimationPlayback& clock, float& elapsed, bool playing, double duration,
    double speed, double curveDuration, bool loop, float deltaTime, const FloatCurve* curve) {
    AnimationPlaybackSettings settings;
    settings.duration = duration;
    settings.baseSpeed = speed;
    settings.curveDuration = curveDuration;
    settings.loop = loop;
    // 逆再生の初期位置は終端。以後は同じ独立経過秒から積分する。
    settings.initialTime = speed < 0 ? duration : 0;
    const bool seek = elapsed != static_cast<float>(clock.Elapsed());
    // curve編集前の評価結果をevent開始位置に使わない。独立経過秒の精度は維持する。
    clock.Seek(seek ? elapsed : clock.Elapsed(), settings, curve);
    const double begin = clock.UnwrappedTime();
    const double beginElapsed = clock.Elapsed();
    if(playing) {
        clock.Play();
    } else {
        clock.Pause();
    }
    clock.Update(deltaTime, settings, curve);
    elapsed = static_cast<float>(clock.Elapsed());
    if(playing && std::isfinite(deltaTime) && deltaTime > 0) {
        return AdvancedRange{begin, clock.UnwrappedTime(), AnimationTraversal(beginElapsed, clock.Elapsed(), settings, curve)};
    }
    return {};
}
void Reselect(const std::string& reference, std::string& active, float& elapsed, AnimationPlayback& clock) {
    if(active != reference) {
        if(!active.empty()) {
            elapsed = 0;
        }
        clock.Stop();
        active = reference;
    }
}
}
void EvaluateAnimationComponents(Scene& scene, float deltaTime, AnimationClipAssets& clips,
    CurveAssets& curves, AnimationLayerAssets& layerAssets, AnimationPropertyEvaluator& properties, const AnimationRangeSink& events) {
    const auto resolve = [&](const std::string& reference) -> Result<const AnimationClipDocument*> {
        auto loaded = clips.Load(reference);
        if(!loaded) {
            return std::unexpected(loaded.error());
        }
        return loaded->get();
    };
    std::vector<Entity> propertyEntities;
    scene.ForEach<KeyframeAnimationComponent>([&](Entity entity, const auto&) {
        propertyEntities.push_back(entity);
    });
    std::ranges::sort(propertyEntities, [](Entity first, Entity second) {
        const auto* a = first.GetComponent<PersistentIdComponent>();
        const auto* b = second.GetComponent<PersistentIdComponent>();
        return std::tuple{a ? 0 : 1, a ? a->value.high : uint64_t{0}, a ? a->value.low : uint64_t{0}, first.GetId().index} <
            std::tuple{b ? 0 : 1, b ? b->value.high : uint64_t{0}, b ? b->value.low : uint64_t{0}, second.GetId().index};
    });
    const auto keyframe = [&](Entity entity, KeyframeAnimationComponent& component) {
        if(component.runtimeDriven) {
            const auto applied = properties.Apply(entity, component.samples, resolve, true);
            if(!applied) {
                properties.Warn(applied.error().GetMessageText());
            }
            if(events) {
                for(const auto& range : component.timelineDriven ? component.timelineRanges : component.controller.EventRanges()) {
                    auto clip = clips.Load(range.asset);
                    if(clip) {
                        events(entity, range.asset, **clip, range.begin, range.end, range.loop);
                    }
                }
            }
            return;
        }
        if(component.clipAsset.empty()) {
            return;
        }
        auto asset = clips.Load(component.clipAsset);
        if(!component.IsValid() || !asset || EffectiveAnimationClipKind(**asset) != AnimationClipKind::Keyframe) {
            properties.Warn("KeyframeAnimation: invalid or incompatible clip " + component.clipAsset);
            return;
        }
        Reselect(component.clipAsset, component.activeClip, component.elapsed, component.playback);
        const auto curve = SpeedCurve(component.speedCurveAsset, curves, properties);
        const auto range = Advance(component.playback, component.elapsed, component.playing, (*asset)->duration,
            component.playbackSpeed, component.speedCurveDuration, component.loop, deltaTime, curve.get());
        const std::vector<AnimationPoseSample> samples{{component.clipAsset, 0, static_cast<float>(component.playback.Time()), 1, false}};
        const auto result = properties.Apply(entity, samples, resolve, true);
        if(!result) {
            properties.Warn(result.error().GetMessageText());
        }
        if(events && range) {
            for(const auto& [begin, end] : range->traversed) {
                events(entity, component.clipAsset, **asset, begin, end, component.loop);
            }
        }
    };
    for(auto entity : propertyEntities) {
        if(auto* component = entity.GetComponent<KeyframeAnimationComponent>()) {
            keyframe(entity, *component);
        }
    }
    scene.ForEach<SkeletalAnimationComponent>([&](Entity entity, auto& component) {
        component.rootRanges.clear();
        const float previousElapsed = component.runtimeDriven ? static_cast<float>(component.controlledBeginElapsed) : component.elapsed;
        const auto evaluateBase = [&] {
        if(component.runtimeDriven) {
            if(events) {
                for(const auto& range : component.timelineDriven ? component.timelineRanges : component.controller.EventRanges()) {
                    auto clip = clips.Load(range.asset);
                    if(clip) {
                        events(entity, range.asset, **clip, range.begin, range.end, range.loop);
                    }
                }
            }
            for(const auto& range : component.timelineDriven ? component.timelineRanges : component.controller.MotionRanges()) {
                RootMotionRange motion{range.asset, range.begin, range.end, range.loop, range.weight};
                if(component.timelineDriven) {
                    motion.mode = PoseBlendMode::Additive;
                    motion.repeats = range.repeats;
                    motion.sequential = true;
                }
                component.rootRanges.push_back(std::move(motion));
            }
            return;
        }
        component.samples.clear();
        if(component.clipAsset.empty()) {
            return;
        }
        auto asset = clips.Load(component.clipAsset);
        if(!component.IsValid() || !asset || EffectiveAnimationClipKind(**asset) != AnimationClipKind::Skeletal) {
            properties.Warn("SkeletalAnimation: invalid or incompatible clip " + component.clipAsset);
            return;
        }
        if(entity.GetComponent<AnimationPlayerComponent>()) {
            properties.Warn("SkeletalAnimation overrides legacy AnimationPlayer pose on the same entity");
        }
        const bool sourceChanged = component.clipAsset != component.activeClip;
        if(sourceChanged) {
            component.frozenSource.clear();
        }
        Reselect(component.clipAsset, component.activeClip, component.elapsed, component.playback);
        if(!component.transitioning && (sourceChanged || component.elapsed != static_cast<float>(component.playback.Elapsed()))) {
            // 手動blendのscrubは同じ独立経過秒へ移す。通常更新では二つのclipの完了を独立に扱う。
            component.blendElapsed = component.elapsed;
        }
        const auto curve = SpeedCurve(component.speedCurveAsset, curves, properties);
        const auto sourceRange = Advance(component.playback, component.elapsed, component.playing, (*asset)->duration,
            component.playbackSpeed, component.speedCurveDuration, component.loop, deltaTime, curve.get());
        component.samples.push_back({component.clipAsset, 0, static_cast<float>(component.playback.Time()), 1, false});
        if(!component.frozenSource.empty()) {
            component.samples = component.frozenSource;
        }
        if(component.blendClipAsset.empty()) {
            if(sourceRange) {
                component.rootRanges.push_back({component.clipAsset, sourceRange->first, sourceRange->second, component.loop});
            }
            if(events && sourceRange && component.frozenSource.empty()) {
                for(const auto& [begin, end] : sourceRange->traversed) {
                    events(entity, component.clipAsset, **asset, begin, end, component.loop);
                }
            }
            return;
        }
        auto second = clips.Load(component.blendClipAsset);
        if(!second || EffectiveAnimationClipKind(**second) != AnimationClipKind::Skeletal) {
            if(sourceRange && component.frozenSource.empty()) {
                component.rootRanges.push_back({component.clipAsset, sourceRange->first, sourceRange->second, component.loop});
            }
            properties.Warn("SkeletalAnimation: invalid blend destination; keeping source " + component.blendClipAsset);
            if(events && sourceRange && component.frozenSource.empty()) {
                for(const auto& [begin, end] : sourceRange->traversed) {
                    events(entity, component.clipAsset, **asset, begin, end, component.loop);
                }
            }
            return;
        }
        Reselect(component.blendClipAsset, component.activeBlend, component.blendElapsed, component.blendPlayback);
        const auto destinationRange = Advance(component.blendPlayback, component.blendElapsed, component.playing, (*second)->duration,
            component.blendSpeed, component.speedCurveDuration, component.blendLoop, deltaTime, curve.get());
        if(component.transitioning && component.playing && std::isfinite(deltaTime) && deltaTime > 0) {
            component.transitionElapsed += deltaTime;
        }
        if(component.transitioning) {
            component.blendWeight = component.crossfadeDuration <= 0 ? 1.0f :
                (std::min)(1.0f, component.transitionElapsed / component.crossfadeDuration);
        }
        for(auto& sample : component.samples) {
            sample.weight *= 1 - component.blendWeight;
        }
        component.samples.push_back({component.blendClipAsset, 0, static_cast<float>(component.blendPlayback.Time()), component.blendWeight, false});
        if(sourceRange && component.frozenSource.empty()) {
            component.rootRanges.push_back({component.clipAsset, sourceRange->first, sourceRange->second, component.loop, 1 - component.blendWeight});
        }
        if(destinationRange) {
            component.rootRanges.push_back({component.blendClipAsset, destinationRange->first, destinationRange->second, component.blendLoop, component.blendWeight});
        }
        if(events && sourceRange && component.blendWeight < 1 && component.frozenSource.empty()) {
            for(const auto& [begin, end] : sourceRange->traversed) {
                events(entity, component.clipAsset, **asset, begin, end, component.loop);
            }
        }
        if(events && destinationRange && component.blendWeight > 0) {
            for(const auto& [begin, end] : destinationRange->traversed) {
                events(entity, component.blendClipAsset, **second, begin, end, component.blendLoop);
            }
        }
        if(component.transitioning && component.blendWeight >= 1) {
            // 遷移先clockをそのまま移す。clip再選択検出による0秒への巻戻しを防ぐ。
            component.clipAsset = component.blendClipAsset;
            component.activeClip = component.clipAsset;
            component.playback = component.blendPlayback;
            component.elapsed = component.blendElapsed;
            component.playbackSpeed = component.blendSpeed;
            component.loop = component.blendLoop;
            component.blendClipAsset.clear();
            component.activeBlend.clear();
            component.blendWeight = 0;
            component.transitioning = false;
            component.frozenSource.clear();
        }
        };
        evaluateBase();
        if(component.timelineDriven) {
            return;
        }
        if(component.layerAsset.empty()) {
            component.activeLayers.reset();
            component.layers.clear();
            return;
        }
        auto layerDocument = layerAssets.Load(component.layerAsset);
        if(!layerDocument) {
            properties.Warn(layerDocument.error().GetMessageText());
            return;
        }
        if(component.activeLayers != *layerDocument) {
            const auto previousDocument = component.activeLayers;
            auto previousStates = std::move(component.layers);
            std::vector<bool> used(previousStates.size());
            component.activeLayers = *layerDocument;
            component.layers.assign((*layerDocument)->layers.size(), {});
            for(size_t index = 0; index < component.layers.size(); ++index) {
                component.layers[index].elapsed = previousElapsed;
                if(!previousDocument) {
                    continue;
                }
                const auto& definition = (*layerDocument)->layers[index];
                size_t match = previousStates.size();
                for(size_t old = 0; old < previousStates.size(); ++old) {
                    const auto& previous = previousDocument->layers[old];
                    if(!used[old] && previous.name == definition.name && previous.clip == definition.clip) {
                        match = old;
                        break;
                    }
                }
                if(match == previousStates.size() && index < previousStates.size() && !used[index] &&
                    previousDocument->layers[index].clip == definition.clip) {
                    match = index;
                }
                if(match < previousStates.size()) {
                    // weight/mask/speedの保存や並替えで独立時計をbase clockへ戻さない。
                    component.layers[index] = std::move(previousStates[match]);
                    used[match] = true;
                }
            }
        }
        for(size_t index = 0; index < (*layerDocument)->layers.size(); ++index) {
            const auto& definition = (*layerDocument)->layers[index];
            auto& state = component.layers[index];
            if(!definition.enabled) {
                continue;
            }
            auto clip = clips.Load(definition.clip);
            if(!clip || EffectiveAnimationClipKind(**clip) != AnimationClipKind::Skeletal) {
                properties.Warn("Animation layer has an invalid skeletal clip: " + definition.clip);
                continue;
            }
            const auto speedCurve = SpeedCurve(definition.speedCurve, curves, properties);
            const auto range = Advance(state.clock, state.elapsed, component.playing, (*clip)->duration,
                definition.speed, definition.curveDuration, definition.loop, deltaTime, speedCurve.get());
            float weight = definition.weight;
            if(!definition.weightCurve.empty()) {
                const auto weightCurve = SpeedCurve(definition.weightCurve, curves, properties);
                if(weightCurve) {
                    const double duration = definition.curveDuration > 0 ? definition.curveDuration : (*clip)->duration;
                    const double input = duration > 0 ? std::clamp(state.clock.Elapsed() / duration, 0.0, 1.0) : 0;
                    // weightのclampは使用側だけで行い、汎用curveを変更しない。
                    weight *= static_cast<float>(std::clamp(weightCurve->Evaluate(input), 0.0, 1.0));
                }
            }
            AnimationPoseSample sample{definition.clip, 0, static_cast<float>(state.clock.Time()), weight, false};
            sample.mode = definition.mode;
            sample.maskPaths = definition.mask;
            component.samples.push_back(std::move(sample));
            if(range) {
                component.rootRanges.push_back({definition.clip, range->first, range->second, definition.loop, weight, definition.mode, definition.mask});
            }
            if(events && range && weight > 0) {
                for(const auto& [begin, end] : range->traversed) {
                    events(entity, definition.clip, **clip, begin, end, definition.loop);
                }
            }
        }
    });
}
}
