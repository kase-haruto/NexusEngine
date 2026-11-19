#include "Runtime/Curves/CurveSerializer.h"
#include "Runtime/Animation/AnimationPlayback.h"
#include "Runtime/Animation/AnimationClipAssets.h"
#include "Runtime/Curves/CurveAssets.h"
#include "Runtime/Level/Level.h"
#include "Runtime/Scene/Components/KeyframeAnimationComponent.h"
#include "Runtime/Scene/Components/SkeletalAnimationComponent.h"
#include "Runtime/Scene/Components/PrimitiveRenderComponent.h"
#include "Runtime/Scene/Components/CurveBindingComponent.h"
#include "Runtime/Scene/Components/CameraComponent.h"
#include "Runtime/Scene/Components/CameraEffectComponent.h"
#include "Runtime/Scene/Components/TimelinePlayerComponent.h"
#include "Runtime/Rendering/RenderSceneExtractor.h"
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace NexusEngine::Tests {
namespace {
void Require(bool condition, const char* message) {
    if(!condition) {
        throw std::runtime_error(message);
    }
}
void Near(double actual, double expected) {
    Require(std::abs(actual - expected) < 1e-8, "Curve/playback value differs");
}
}
/** \brief Graphics/ImGuiに依存しないCurveと共通clockの境界・再現性テスト。 */
void RunCurveTests() {
    FloatCurve curve;
    Near(curve.Evaluate(0), 1);
    Near(curve.Integrate(-2, 3), 5);
    Near(curve.Evaluate(std::numeric_limits<double>::quiet_NaN()), 1);
    Near(curve.Integrate(0, std::numeric_limits<double>::infinity()), 0);
    Require(curve.SetKeys({{2, -3}}).has_value(), "Single key rejected");
    Near(curve.Evaluate(-100), -3);
    Near(curve.Integrate(0, 4), -12);
    Require(curve.SetKeys({{0, 0}, {1, 2}}).has_value(), "Linear keys rejected");
    Near(curve.Evaluate(.25), .5);
    Near(curve.Integrate(0, 1), 1);
    Near(curve.Integrate(1, 0), -1);
    Near(curve.Integrate(-1, 2), 3);
    const auto original = curve;
    Require(!curve.SetKeys({{0, 1}, {0, 2}}), "Duplicate X accepted");
    Require(!curve.SetKeys({{1, 1}, {0, 2}}), "Unsorted X accepted");
    Require(!curve.SetKeys({{0, std::numeric_limits<double>::quiet_NaN()}}), "NaN accepted");
    Require(!curve.SetKeys({{0, std::numeric_limits<double>::infinity()}}), "Infinity accepted");
    Require(curve == original, "Rejected edit changed curve");
    CurveKey first{0, 0};
    first.interpolation = CurveInterpolation::Cubic;
    CurveKey last{1, 1};
    Require(curve.SetKeys({first, last}).has_value(), "Cubic rejected");
    Near(curve.Evaluate(.5), .5);
    Near(curve.Integrate(0, 1), .5);
    first.interpolation = CurveInterpolation::Bezier;
    first.outWeight = .1;
    last.inWeight = .8;
    Require(curve.SetKeys({first, last}).has_value(), "Weighted Bezier rejected");
    Near(curve.Evaluate(.2375), .5);
    Near(curve.Integrate(0, 1), .71);
    const auto weighted = curve;
    Require(curve.InsertKey(.2375, .5).has_value(), "Bezier split failed");
    for(int sample = 0; sample <= 128; ++sample) {
        Near(curve.Evaluate(sample / 128.0), weighted.Evaluate(sample / 128.0));
    }
    Near(curve.Integrate(0, 1), .71);
    const auto weightedText = SerializeFloatCurve(weighted);
    const auto restoredWeighted = ParseFloatCurve(weightedText);
    Require(restoredWeighted && *restoredWeighted == weighted, "Weighted curve roundtrip failed");
    first.y = -1;
    Require(curve.SetKeys({first, last}).has_value(), "Signed Bezier rejected");
    const auto roots = curve.MonotonicIntegralBreaks(0, 1);
    Require(roots.size() == 3, "Bezier speed zero was not isolated");
    Near(roots[1], .2375);
    first.y = 0;
    first.interpolation = CurveInterpolation::Constant;
    Require(curve.SetKeys({first, last}).has_value(), "Constant rejected");
    Near(curve.Evaluate(.999), 0);
    Near(curve.Evaluate(1), 1);
    Near(curve.Integrate(0, 1), 0);
    first = {0, 0, 0, 0, CurveInterpolation::Cubic, CurveTangentMode::ClampedAuto};
    CurveKey middle{.01, 1, 0, 0, CurveInterpolation::Cubic, CurveTangentMode::ClampedAuto};
    last = {1, 1.1, 0, 0, CurveInterpolation::Cubic, CurveTangentMode::ClampedAuto};
    Require(curve.SetKeys({first, middle, last}).has_value(), "Auto rejected");
    for(int i = 0; i <= 1000; ++i) {
        const double value = curve.Evaluate(static_cast<double>(i) / 1000);
        Require(value >= 0 && value <= 1.1, "Clamped auto overshot");
    }
    const auto folder = std::filesystem::absolute("Generated/CurveTests");
    std::filesystem::create_directories(folder);
    Require(SaveFloatCurve(folder / "profile.curve", curve).has_value(), "Save failed");
    const auto loaded = LoadFloatCurve(folder / "profile.curve");
    Require(loaded.has_value() && *loaded == curve, "Curve round trip failed");
    {
        std::ofstream file(folder / "future.curve");
        file << "{\"version\":999,\"keys\":[]}";
    }
    Require(!LoadFloatCurve(folder / "future.curve"), "Unknown version accepted");

    Require(curve.SetKeys({{0, 0}, {1, 2}}).has_value(), "Speed curve rejected");
    AnimationPlaybackSettings settings;
    settings.duration = 2;
    settings.curveDuration = 1;
    AnimationPlayback a, b;
    a.Play();
    for(int i = 0; i < 75; ++i) {
        a.Update(.01, settings, &curve);
    }
    b.Seek(.75, settings, &curve);
    Near(a.Time(), b.Time());
    Near(a.Time(), .5625);
    b.Seek(1.5, settings, &curve);
    Near(b.Time(), 0);
    Near(b.UnwrappedTime(), 2);
    FloatCurve extended;
    Require(extended.SetKeys({{0, 1}, {2, 3}}).has_value(), "Generic extended curve rejected");
    AnimationPlayback extendedPlayback;
    AnimationPlaybackSettings extendedSettings;
    extendedSettings.duration = 10;
    extendedSettings.curveDuration = 1;
    extendedPlayback.Seek(2, extendedSettings, &extended);
    Near(extendedPlayback.Time(), 3.5);
    b.Pause();
    Require(curve.SetKeys({{0, 1}, {1, 1}}).has_value(), "Override rejected");
    b.Update(1, settings, &curve);
    Near(b.Time(), 1.5);
    AnimationPlayback c;
    c.Seek(.5, settings);
    Near(c.Time(), .5);
    Near(a.Time(), .5625);
    settings.baseSpeed = -1;
    c.Seek(.5, settings);
    Near(c.Time(), 1.5);
    settings.duration = 0;
    c.Seek(1, settings);
    Near(c.Time(), 0);
    Require(c.IsCompleted(), "Zero duration did not complete");

    // 途中で終端を越えて戻る場合も、大きなSeekと分割更新で同じ終端に停止する。
    settings = {};
    settings.loop = false;
    Require(curve.SetKeys({{0, 8}, {.5, -8}, {1, -8}}).has_value(), "Reverse curve rejected");
    a.Stop();
    a.Play();
    for(int i = 0; i < 100; ++i) {
        a.Update(.01, settings, &curve);
    }
    b.Seek(1, settings, &curve);
    Near(a.Time(), 1);
    Near(b.Time(), a.Time());
    Require(a.IsCompleted() && b.IsCompleted(), "Crossed end not detected");
    c.Stop();
    Near(c.Elapsed(), 0);
    Near(c.Time(), 0);
    CurveAssets cache;
    cache.SetRoot(folder);
    const auto saved = cache.Load("profile.curve");
    const auto shared = cache.Load("./PROFILE.curve");
    Require(saved && shared && *saved == *shared, "Curve asset is not shared");
    Require(cache.Override("profile.curve", curve).has_value(), "Override failed");
    const auto draft = cache.Load("profile.curve");
    Require(draft && *draft != *saved && **saved == *loaded, "Preview changed saved snapshot");
    cache.ClearOverrides();
    Require(cache.Load("profile.curve").value() == *saved, "Preview release lost saved asset");
    Require(!cache.Load("../outside.curve"), "Asset escaped root");

    AnimationClipDocument clip;
    clip.kind = AnimationClipKind::Keyframe;
    clip.events.push_back({.25f, "Tick", "new-component"});
    AnimationTrack track;
    track.keys = {{0, {0, 0, 0}, {}, {}}, {1, {10, 0, 0}, {}, {}}};
    clip.tracks.push_back(track);
    auto bad = track;
    bad.component = "MissingComponent";
    clip.tracks.push_back(bad);
    Require(SaveAnimationDocument(folder / "move.nxanim", clip).has_value(), "New clip save failed");
    const auto roundTrip = LoadAnimationClipDocument(folder / "move.nxanim");
    Require(roundTrip && roundTrip->kind == AnimationClipKind::Keyframe, "Clip kind lost");
    AnimationClipAssets clipCache;
    clipCache.SetRoot(folder);
    const auto firstAsset = clipCache.Load("move.nxanim");
    Require(firstAsset && clipCache.Load("move.nxanim").value() == *firstAsset, "Clip asset not shared");
    Level level;
    Require(level.LoadEmpty().has_value(), "Cannot create test level");
    level.Animations().SetRoot(folder);
    int eventCount = 0;
    bool lastEventReverse = false;
    level.Animations().SetEventHandler([&](const auto& event) {
        Require(event.name == "Tick", "Unexpected new-component event");
        ++eventCount;
        lastEventReverse = event.reverse;
    });
    auto firstEntity = level.CreateEntity("First");
    auto secondEntity = level.CreateEntity("Second");
    auto* firstPlayer = firstEntity.AddComponent<KeyframeAnimationComponent>();
    auto* secondPlayer = secondEntity.AddComponent<KeyframeAnimationComponent>();
    firstPlayer = firstEntity.GetComponent<KeyframeAnimationComponent>();
    firstPlayer->clipAsset = secondPlayer->clipAsset = "move.nxanim";
    secondPlayer->playing = false;
    secondPlayer->elapsed = .25f;
    Require(level.Update(.5f).has_value(), "New keyframe update failed");
    Near(firstEntity.GetComponent<TransformComponent>()->translation.x, 5);
    Near(secondEntity.GetComponent<TransformComponent>()->translation.x, 2.5);
    Require(eventCount == 1, "New component event range was not delivered");
    Require(!level.Animations().Diagnostics().empty(), "Invalid track has no diagnostic");
    firstPlayer = firstEntity.GetComponent<KeyframeAnimationComponent>();
    firstPlayer->playing = false;
    firstPlayer->speedCurveAsset = "profile.curve";
    FloatCurve slow;
    Require(slow.SetKeys({{0, .5}, {1, .5}}).has_value(), "Slow curve failed");
    Require(level.Animations().Curves().Override("profile.curve", slow).has_value(), "Runtime override failed");
    Require(level.Update(0).has_value(), "Curve edit reevaluation failed");
    Near(firstEntity.GetComponent<TransformComponent>()->translation.x, 2.5);
    Require(eventCount == 1, "Curve edit or pause emitted an event");
    firstEntity.RemoveComponent<KeyframeAnimationComponent>();
    Require(level.Update(0).has_value(), "Component removal failed");
    Near(firstEntity.GetComponent<TransformComponent>()->translation.x, 0);

    TimelineDocument propertyTimeline;
    propertyTimeline.duration = 2;
    TimelineTrack propertyTrack;
    propertyTrack.clips = {{"move.nxanim", 0, 1}};
    propertyTimeline.tracks.push_back(propertyTrack);
    Require(SaveAnimationDocument(folder / "properties.nxtimeline", propertyTimeline).has_value(), "Timeline fixture save failed");
    secondEntity.GetComponent<KeyframeAnimationComponent>()->elapsed = .9f;
    auto* propertyPlayer = secondEntity.AddComponent<TimelinePlayerComponent>();
    propertyPlayer->timelineAsset = "properties.nxtimeline";
    propertyPlayer->playing = false;
    propertyPlayer->time = .25f;
    Require(level.Update(0).has_value(), "Typed property Timeline update failed");
    Near(secondEntity.GetComponent<TransformComponent>()->translation.x, 2.5);
    secondEntity.GetComponent<TimelinePlayerComponent>()->time = 1.25f;
    Require(level.Update(0).has_value(), "Typed Timeline gap failed");
    Near(secondEntity.GetComponent<TransformComponent>()->translation.x, 0);
    secondEntity.RemoveComponent<TimelinePlayerComponent>();
    Require(level.Update(0).has_value(), "Typed Timeline release failed");
    Near(secondEntity.GetComponent<TransformComponent>()->translation.x, 9);
    FloatCurve reversal;
    Require(reversal.SetKeys({{0, 2}, {1, -2}}).has_value(), "Reversing speed curve failed");
    Require(level.Animations().Curves().Override("reversal.curve", reversal).has_value(), "Reversal override failed");
    auto eventEntity = level.CreateEntity("Reversal Events");
    auto* eventPlayer = eventEntity.AddComponent<KeyframeAnimationComponent>();
    eventPlayer->clipAsset = "move.nxanim";
    eventPlayer->speedCurveAsset = "reversal.curve";
    const int previousEvents = eventCount;
    Require(level.Update(1).has_value(), "Reversal interval update failed");
    Require(eventCount == previousEvents + 1 && !lastEventReverse, "Net-zero frame lost its forward event crossing");
    eventPlayer = eventEntity.GetComponent<KeyframeAnimationComponent>();
    eventPlayer->Seek(.5f);
    Require(level.Update(0).has_value(), "Reversal Seek failed");
    Require(eventCount == previousEvents + 1, "Seek emitted reversal events");
    Require(level.Update(.5f).has_value(), "Default reverse event policy failed");
    Require(eventCount == previousEvents + 1, "Reverse events were not opt-in");
    eventPlayer = eventEntity.GetComponent<KeyframeAnimationComponent>();
    eventPlayer->reverseEvents = true;
    eventPlayer->Seek(.5f);
    Require(level.Update(0).has_value() && level.Update(.5f).has_value(), "Opt-in reverse event update failed");
    Require(eventCount == previousEvents + 2 && lastEventReverse, "Reverse event crossing/direction was lost");
    eventEntity.GetComponent<KeyframeAnimationComponent>()->Pause();

    AnimationClipDocument skeleton;
    skeleton.kind = AnimationClipKind::Skeletal;
    Require(SaveAnimationDocument(folder / "idle.nxanim", skeleton).has_value(), "Skeleton clip save failed");
    skeleton.duration = 2;
    Require(SaveAnimationDocument(folder / "walk.nxanim", skeleton).has_value(), "Skeleton destination save failed");
    auto* skeletal = firstEntity.AddComponent<SkeletalAnimationComponent>();
    skeletal->clipAsset = "idle.nxanim";
    skeletal->CrossFade("walk.nxanim", 1, 2, false);
    Require(level.Update(.5f).has_value(), "Crossfade update failed");
    skeletal = firstEntity.GetComponent<SkeletalAnimationComponent>();
    Near(skeletal->blendWeight, .5);
    Near(skeletal->blendPlayback.Time(), 1);
    Require(skeletal->samples.size() == 2, "Blend samples missing");
    skeletal->Pause();
    Require(level.Update(.5f).has_value(), "Crossfade pause failed");
    Near(skeletal->blendWeight, .5);
    skeletal->Play();
    Require(level.Update(.5f).has_value(), "Crossfade completion failed");
    skeletal = firstEntity.GetComponent<SkeletalAnimationComponent>();
    Require(skeletal->clipAsset == "walk.nxanim" && !skeletal->transitioning, "Destination not promoted");
    Near(skeletal->playback.Time(), 2);
    skeletal->Stop();
    skeletal->clipAsset = "idle.nxanim";
    skeletal->CrossFade("walk.nxanim", 1, 2, false);
    Require(level.Update(.75f).has_value(), "Interrupted crossfade setup failed");
    skeletal = firstEntity.GetComponent<SkeletalAnimationComponent>();
    const auto interrupted = skeletal->samples;
    skeletal->CrossFade("idle.nxanim", 1);
    Require(level.Update(0).has_value(), "Crossfade interruption failed");
    skeletal = firstEntity.GetComponent<SkeletalAnimationComponent>();
    Require(skeletal->frozenSource.size() == 2 && skeletal->samples.size() == 3, "Interrupted blended pose was not preserved");
    for(size_t index = 0; index < interrupted.size(); ++index) {
        Near(skeletal->samples[index].time, interrupted[index].time);
        Near(skeletal->samples[index].weight, interrupted[index].weight);
        Require(skeletal->samples[index].asset == interrupted[index].asset, "Interrupted source clip changed");
    }
    skeletal->CrossFade("missing.nxanim", 1);
    Require(level.Update(.1f).has_value(), "Invalid destination aborted the frame");
    Require(firstEntity.GetComponent<SkeletalAnimationComponent>()->samples.size() == 2, "Invalid destination replaced frozen source pose");
    skeletal = firstEntity.GetComponent<SkeletalAnimationComponent>();
    skeletal->Stop();
    skeletal->blendClipAsset.clear();
    skeletal->blendWeight = 0;
    skeletal->clipAsset = "idle.nxanim";
    AnimationLayerDocument layerDocument;
    AnimationLayerDefinition layerDefinition;
    layerDefinition.clip = "walk.nxanim";
    layerDefinition.speed = 2;
    layerDefinition.weight = .5f;
    layerDefinition.mode = PoseBlendMode::Additive;
    layerDocument.layers.push_back(layerDefinition);
    Require(SaveAnimationLayers(folder / "upper.nxlayers", layerDocument).has_value(), "Layer document save failed");
    skeletal->layerAsset = "upper.nxlayers";
    skeletal->Play();
    Require(level.Update(.25f).has_value(), "Layer playback failed");
    skeletal = firstEntity.GetComponent<SkeletalAnimationComponent>();
    Require(skeletal->samples.size() == 2 && skeletal->samples[1].mode == PoseBlendMode::Additive, "Layer definition was not applied");
    Near(skeletal->samples[1].time, .5);
    Near(skeletal->samples[1].weight, .5);
    skeletal->Pause();
    Require(level.Update(.25f).has_value(), "Layer pause failed");
    Near(skeletal->layers[0].clock.Time(), .5);
    skeletal->Seek(.5f);
    Require(level.Update(0).has_value(), "Layer seek failed");
    Near(skeletal->layers[0].clock.Time(), 1);
    skeletal->layers[0].elapsed = .375f;
    Require(level.Update(0).has_value(), "Independent layer elapsed fixture failed");
    Near(skeletal->layers[0].clock.Time(), .75);
    layerDocument.layers[0].weight = .25f;
    Require(SaveAnimationLayers(folder / "upper.nxlayers", layerDocument).has_value(), "Updated layer save failed");
    level.Animations().Invalidate();
    Require(level.Update(0).has_value(), "Updated layer reload failed");
    Near(skeletal->layers[0].clock.Time(), .75);
    Near(skeletal->samples[1].weight, .25);
    AnimatorDocument controllerDefinition;
    controllerDefinition.entry = "Idle";
    controllerDefinition.parameters = {{"Move", "Bool", 0}};
    AnimatorState idleState, walkState;
    idleState.name = "Idle";
    idleState.asset = "idle.nxanim";
    walkState.name = "Walk";
    walkState.asset = "walk.nxanim";
    controllerDefinition.states = {idleState, walkState};
    AnimatorTransition startWalking;
    startWalking.from = "Idle";
    startWalking.to = "Walk";
    startWalking.duration = .5f;
    startWalking.conditions = {{"Move", "True", 0}};
    controllerDefinition.transitions.push_back(startWalking);
    AnimatorTransition stopWalking;
    stopWalking.from = "Any State";
    stopWalking.to = "Idle";
    stopWalking.duration = .5f;
    stopWalking.conditions = {{"Move", "False", 0}};
    controllerDefinition.transitions.push_back(stopWalking);
    Require(SaveAnimationDocument(folder / "states.nxcontroller", controllerDefinition).has_value(), "Controller save failed");
    skeletal->Stop();
    skeletal->layerAsset.clear();
    skeletal->controllerAsset = "states.nxcontroller";
    skeletal->Play();
    Require(level.Update(.1f).has_value(), "New controller startup failed");
    skeletal = firstEntity.GetComponent<SkeletalAnimationComponent>();
    Require(skeletal->runtimeDriven && skeletal->controller.State() == "Idle", "Controller did not drive skeletal component");
    skeletal->controller.SetBool("Move", true);
    Require(level.Update(.1f).has_value(), "Controller transition failed");
    Require(skeletal->controller.NextState() == "Walk" && skeletal->samples.size() == 2, "Controller did not blend shared clips");
    const auto beforeInterruption = skeletal->samples;
    skeletal->controller.SetBool("Move", false);
    Require(level.Update(0).has_value(), "Controller interrupt failed");
    Require(skeletal->controller.NextState() == "Idle" && skeletal->samples.size() == 3, "Controller interruption did not preserve mixed source");
    Near(skeletal->samples[0].weight, beforeInterruption[0].weight);
    Near(skeletal->samples[1].weight, beforeInterruption[1].weight);
    skeletal->Pause();
    const auto pausedControllerTime = skeletal->controller.Elapsed();
    Require(level.Update(.2f).has_value(), "Controller pause failed");
    Near(skeletal->controller.Elapsed(), pausedControllerTime);
    skeletal->Seek(.5f);
    Require(level.Update(0).has_value(), "Controller seek failed");
    Near(skeletal->controller.Elapsed(), .5);
    {
        const float motionData[]{0, 1, 0, 0, 0, 1, 0, 0};
        std::ofstream output(folder / "root.bin", std::ios::binary);
        output.write(reinterpret_cast<const char*>(motionData), sizeof(motionData));
    }
    {
        std::ofstream output(folder / "root.gltf");
        output << R"({"asset":{"version":"2.0"},"buffers":[{"uri":"root.bin","byteLength":32}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":8},{"buffer":0,"byteOffset":8,"byteLength":24}],"accessors":[{"bufferView":0,"componentType":5126,"count":2,"type":"SCALAR","min":[0],"max":[1]},{"bufferView":1,"componentType":5126,"count":2,"type":"VEC3"}],"nodes":[{"name":"Root"}],"scenes":[{"nodes":[0]}],"scene":0,"animations":[{"name":"Travel","samplers":[{"input":0,"output":1,"interpolation":"LINEAR"}],"channels":[{"sampler":0,"target":{"node":0,"path":"translation"}}]}]})";
    }
    AnimationClipDocument rootClip;
    rootClip.kind = AnimationClipKind::Skeletal;
    rootClip.source = "root.gltf";
    rootClip.clip = "Travel";
    Require(SaveAnimationDocument(folder / "root.nxanim", rootClip).has_value(), "Root clip save failed");
    auto rootEntity = level.CreateEntity("Root Motion");
    auto* rootModel = rootEntity.AddComponent<PrimitiveRenderComponent>();
    rootModel->meshAsset = "root.gltf";
    auto* rootPlayer = rootEntity.AddComponent<SkeletalAnimationComponent>();
    rootPlayer->clipAsset = "root.nxanim";
    rootPlayer->rootMotion = true;
    rootPlayer->rootMotionJoint = "4:Root/";
    Require(level.Update(.25f).has_value(), "Root Motion Entity update failed");
    Near(rootEntity.GetComponent<TransformComponent>()->translation.x, .25);
    rootPlayer = rootEntity.GetComponent<SkeletalAnimationComponent>();
    rootPlayer->Pause();
    Require(level.Update(.25f).has_value(), "Root Motion pause failed");
    Near(rootEntity.GetComponent<TransformComponent>()->translation.x, .25);
    rootPlayer->Seek(.75f);
    Require(level.Update(0).has_value(), "Root Motion seek failed");
    Near(rootEntity.GetComponent<TransformComponent>()->translation.x, .25);
    rootPlayer->Play();
    Require(level.Update(.5f).has_value(), "Root Motion loop failed");
    Near(rootEntity.GetComponent<TransformComponent>()->translation.x, .75);
    TimelineDocument motionTimeline;
    motionTimeline.duration = 1;
    TimelineTrack motionTrack;
    motionTrack.clips = {{"root.nxanim", 0, 1}};
    motionTimeline.tracks.push_back(motionTrack);
    Require(SaveAnimationDocument(folder / "motion.nxtimeline", motionTimeline).has_value(), "Motion Timeline save failed");
    auto* timelineMotion = rootEntity.AddComponent<TimelinePlayerComponent>();
    timelineMotion->timelineAsset = "motion.nxtimeline";
    timelineMotion->time = .25f;
    Require(level.Update(.25f).has_value(), "Root Motion Timeline update failed");
    Near(rootEntity.GetComponent<TransformComponent>()->translation.x, 1);
    timelineMotion = rootEntity.GetComponent<TimelinePlayerComponent>();
    timelineMotion->playing = false;
    Require(level.Update(.25f).has_value(), "Root Motion Timeline pause failed");
    Near(rootEntity.GetComponent<TransformComponent>()->translation.x, 1);
    timelineMotion = rootEntity.GetComponent<TimelinePlayerComponent>();
    timelineMotion->loop = true;
    timelineMotion->playing = true;
    timelineMotion->time = .75f;
    Require(level.Update(.5f).has_value(), "Root Motion Timeline wrap failed");
    Near(rootEntity.GetComponent<TransformComponent>()->translation.x, 1.5);
    timelineMotion = rootEntity.GetComponent<TimelinePlayerComponent>();
    timelineMotion->playing = false;
    timelineMotion->time = 0;
    Require(level.Update(0).has_value(), "Root Motion Timeline Seek failed");
    Near(rootEntity.GetComponent<TransformComponent>()->translation.x, 1.5);
    rootEntity.RemoveComponent<TimelinePlayerComponent>();
    rootEntity.GetComponent<SkeletalAnimationComponent>()->Pause();
    auto cameraEntity = level.CreateEntity("Curve Camera");
    auto* camera = cameraEntity.AddComponent<CameraComponent>();
    const float originalFov = camera->verticalFieldOfViewRadians;
    auto* binding = cameraEntity.AddComponent<CurveBindingComponent>();
    binding->curveAsset = "generic.curve";
    binding->manual = true;
    binding->input = .25f;
    FloatCurve generic;
    Require(generic.SetKeys({{0, .5}, {1, 1.5}}).has_value(), "Generic curve keys failed");
    Require(SaveFloatCurve(folder / "generic.curve", generic).has_value(), "Generic curve save failed");
    Require(level.Update(0).has_value(), "Generic Curve binding evaluation failed");
    Near(cameraEntity.GetComponent<CameraComponent>()->verticalFieldOfViewRadians, .75);
    binding = cameraEntity.GetComponent<CurveBindingComponent>();
    binding->channel = 2;
    Require(level.Update(0).has_value(), "Invalid Curve channel crashed");
    Near(cameraEntity.GetComponent<CameraComponent>()->verticalFieldOfViewRadians, originalFov);
    binding = cameraEntity.GetComponent<CurveBindingComponent>();
    binding->channel = 0;
    binding->manual = false;
    binding->loop = false;
    Require(level.Update(.5f).has_value(), "Curve binding clock failed");
    Near(cameraEntity.GetComponent<CameraComponent>()->verticalFieldOfViewRadians, 1);
    binding = cameraEntity.GetComponent<CurveBindingComponent>();
    binding->Pause();
    Require(level.Update(.25f).has_value(), "Curve binding pause failed");
    Near(cameraEntity.GetComponent<CameraComponent>()->verticalFieldOfViewRadians, 1);
    binding = cameraEntity.GetComponent<CurveBindingComponent>();
    binding->playing = true;
    binding->loop = true;
    binding->playbackSpeed = -1;
    binding->elapsed = 0;
    Require(level.Update(.25f).has_value(), "Generic Curve reverse loop failed");
    Near(cameraEntity.GetComponent<CameraComponent>()->verticalFieldOfViewRadians, 1.25);
    auto* effect = cameraEntity.AddComponent<CameraEffectComponent>();
    Require(effect != nullptr, "Camera Effect creation failed");
    binding = cameraEntity.GetComponent<CurveBindingComponent>();
    binding->targetComponent = "CameraEffect";
    binding->targetProperty = "parameters0";
    binding->manual = true;
    binding->input = .25f;
    Require(level.Update(0).has_value(), "Curve Camera Effect binding failed");
    Near(cameraEntity.GetComponent<CameraEffectComponent>()->parameters0[0], .75);
    Near(cameraEntity.GetComponent<CameraEffectComponent>()->parameters0[1], 1);
    RenderScene effectScene;
    Require(RenderSceneExtractor{}.ExtractInto(*level.GetScene(), 1, effectScene).has_value(), "Camera Effect extraction failed");
    Require(effectScene.screenEffects.size() == 1, "Curve Camera Effect was not extracted");
    Near(effectScene.screenEffects[0].parameters[0], .75);
    cameraEntity.RemoveComponent<CurveBindingComponent>();
    Require(level.Update(0).has_value(), "Curve binding removal failed");
    Near(cameraEntity.GetComponent<CameraComponent>()->verticalFieldOfViewRadians, originalFov);
    skeleton.kind = AnimationClipKind::Keyframe;
    skeleton.source = "bad.gltf";
    Require(!ValidateAnimationDocument(skeleton), "Wrong clip kind accepted");
}
}
