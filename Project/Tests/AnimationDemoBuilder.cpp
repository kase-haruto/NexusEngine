#include "Runtime/Level/Level.h"
#include "Runtime/Animation/RetargetProfile.h"
#include "Runtime/Animation/AnimationLayers.h"
#include "Runtime/Curves/CurveSerializer.h"
#include "Graphics/Model/ModelLoader.h"
#include "Runtime/Scene/Components/SkeletalAnimationComponent.h"
#include "Runtime/Scene/Components/CurveBindingComponent.h"
#include "Runtime/Scene/Components/CameraEffectComponent.h"
#include "Runtime/Scene/Components/CameraComponent.h"
#include "Runtime/Scene/Components/PrimitiveRenderComponent.h"
#include "Runtime/Scene/Components/DirectionalLightComponent.h"
#include "Platform/FileSystem/AssetIdentity.h"
#include <stdexcept>

namespace NexusEngine::Tests {
namespace {
void DemoCheck(const Result<void>& result) {
    if(!result) {
        throw std::runtime_error(result.error().GetMessageText());
    }
}
std::string DemoReference(const std::filesystem::path& root, const std::string& path) {
    const auto identity = EnsureAssetIdentity(root, root / path);
    if(!identity) {
        throw std::runtime_error(identity.error().GetMessageText());
    }
    return *identity;
}
}
/** \brief 配布sampleを実際のserializer/reflection/UUID発行で作る。既存Projectの関連付けは操作しない。 */
void BuildAnimationDemo(const std::filesystem::path& project) {
    const auto root = std::filesystem::absolute(project / "Assets");
    auto source = ModelLoader{}.LoadGltf(root / "Models/Source.gltf");
    auto target = ModelLoader{}.LoadGltf(root / "Models/Target.gltf");
    if(!source || !target) {
        throw std::runtime_error("Animation sample models are invalid");
    }
    const auto sourceModel = DemoReference(root, "Models/Source.gltf");
    const auto targetModel = DemoReference(root, "Models/Target.gltf");
    auto profile = SuggestRetargetProfile(*source, *target);
    profile.sourceModel = sourceModel;
    profile.targetModel = targetModel;
    for(auto& joint : profile.joints) {
        joint.translationScale = {1.3f, 1.3f, 1.3f};
    }
    DemoCheck(SaveRetargetProfile(root / "Rigs/Taller.nxretarget", profile));
    const auto retarget = DemoReference(root, "Rigs/Taller.nxretarget");
    FloatCurve speed;
    DemoCheck(speed.SetKeys({{0, .7, 0, 1, CurveInterpolation::Bezier}, {.5, 1.3, 0, 0, CurveInterpolation::Cubic}, {1, 1, -1, 0}}));
    DemoCheck(SaveFloatCurve(root / "Curves/Speed.curve", speed));
    const auto speedReference = DemoReference(root, "Curves/Speed.curve");
    FloatCurve pulse;
    DemoCheck(pulse.SetKeys({{0, .8, 0, 0, CurveInterpolation::Cubic}, {.5, 1.1, 0, 0, CurveInterpolation::Cubic}, {1, .8, 0, 0}}));
    DemoCheck(SaveFloatCurve(root / "Curves/Brightness.curve", pulse));
    const auto brightness = DemoReference(root, "Curves/Brightness.curve");
    std::map<std::string, std::string> references;
    for(const auto* prefix : {"Source", "Target"}) {
        for(const auto* name : {"Idle", "Walk", "Wave", "Smile"}) {
            AnimationClipDocument clip;
            clip.kind = AnimationClipKind::Skeletal;
            clip.source = sourceModel;
            clip.clip = name;
            clip.duration = 2;
            if(std::string_view(prefix) == "Target") {
                clip.retargetProfile = retarget;
            }
            if(std::string_view(name) == "Walk") {
                clip.events = {{.5f, "Step", "Left"}, {1.5f, "Step", "Right"}};
            }
            const auto key = std::string(prefix) + name;
            const auto path = "Animations/" + key + ".nxanim";
            DemoCheck(SaveAnimationDocument(root / path, clip));
            references[key] = DemoReference(root, path);
        }
        AnimationLayerDocument layers;
        AnimationLayerDefinition upper;
        upper.name = "Upper Body / Additive";
        upper.clip = references[std::string(prefix) + "Wave"];
        upper.mode = PoseBlendMode::Additive;
        upper.weight = .25f;
        upper.mask = {"4:Root/4:Bone/"};
        layers.layers.push_back(upper);
        AnimationLayerDefinition face;
        face.name = "Expression / Override";
        face.clip = references[std::string(prefix) + "Smile"];
        face.mode = PoseBlendMode::Override;
        face.weight = .8f;
        face.mask = {"4:Mesh/"};
        layers.layers.push_back(face);
        const auto layerPath = "Layers/" + std::string(prefix) + ".nxlayers";
        DemoCheck(SaveAnimationLayers(root / layerPath, layers));
        references[std::string(prefix) + "Layers"] = DemoReference(root, layerPath);
        AnimatorDocument controller;
        controller.entry = "Locomotion";
        controller.parameters = {{"Speed", "Float", .7f}, {"Wave", "Trigger", 0}};
        AnimatorState locomotion;
        locomotion.name = "Locomotion";
        locomotion.blend = "1D";
        locomotion.parameterX = "Speed";
        locomotion.motions = {{references[std::string(prefix) + "Idle"], 0, 0, 1}, {references[std::string(prefix) + "Walk"], 1, 0, 1}};
        AnimatorState wave;
        wave.name = "Wave";
        wave.asset = references[std::string(prefix) + "Wave"];
        wave.position = {400, 80};
        controller.states = {locomotion, wave};
        AnimatorTransition enter;
        enter.from = "Locomotion";
        enter.to = "Wave";
        enter.conditions = {{"Wave", "True", 0}};
        AnimatorTransition leave;
        leave.from = "Wave";
        leave.to = "Locomotion";
        leave.hasExitTime = true;
        controller.transitions = {enter, leave};
        const auto controllerPath = "Controllers/" + std::string(prefix) + ".nxcontroller";
        DemoCheck(SaveAnimationDocument(root / controllerPath, controller));
        references[std::string(prefix) + "Controller"] = DemoReference(root, controllerPath);
    }
    TimelineDocument timeline;
    timeline.duration = 6;
    TimelineTrack track;
    track.name = "Reusable skeletal clips";
    track.clips = {{references["SourceIdle"], 0, 2}, {references["SourceWalk"], 1.5f, 3, 0, 1, .5f, .5f},
        {references["SourceWave"], 4, 2, 0, 1, .5f, 0}};
    timeline.tracks.push_back(track);
    DemoCheck(SaveAnimationDocument(root / "Timelines/Sequence.nxtimeline", timeline));
    static_cast<void>(DemoReference(root, "Timelines/Sequence.nxtimeline"));
    Level level;
    DemoCheck(level.LoadEmpty());
    level.Animations().SetRoot(root);
    auto camera = level.CreateEntity("Camera / Curve Effect");
    camera.GetComponent<TransformComponent>()->translation = {0, 1.2f, -7};
    if(!camera.AddComponent<CameraComponent>() || !camera.AddComponent<CameraEffectComponent>()) {
        throw std::runtime_error("Sample camera components could not be added");
    }
    auto* binding = camera.AddComponent<CurveBindingComponent>();
    binding->curveAsset = brightness;
    binding->targetComponent = "CameraEffect";
    binding->targetProperty = "parameters0";
    binding->duration = 4;
    auto light = level.CreateEntity("Directional Light");
    auto* lighting = light.AddComponent<DirectionalLightComponent>();
    lighting->light.direction = {.3f, .5f, -1};
    lighting->light.intensity = 4;
    for(const auto* prefix : {"Source", "Target"}) {
        const bool taller = std::string_view(prefix) == "Target";
        auto actor = level.CreateEntity(taller ? "Target / Retargeted 130 percent" : "Source / Shared Clip Assets");
        actor.GetComponent<TransformComponent>()->translation = {taller ? 1.2f : -1.2f, 0, 0};
        auto* render = actor.AddComponent<PrimitiveRenderComponent>();
        render->meshAsset = taller ? targetModel : sourceModel;
        render->staticLighting = false;
        render->tint = taller ? std::array<float, 4>{1, .65f, .4f, 1} : std::array<float, 4>{.5f, .8f, 1, 1};
        auto* player = actor.AddComponent<SkeletalAnimationComponent>();
        player->clipAsset = references[std::string(prefix) + "Walk"];
        player->controllerAsset = references[std::string(prefix) + "Controller"];
        player->layerAsset = references[std::string(prefix) + "Layers"];
        player->speedCurveAsset = speedReference;
        player->speedCurveDuration = 4;
        player->rootMotionJoint = "4:Root/";
    }
    // 実際の新component経路でsampleを評価し、serializerへ誤った参照を残さない。
    DemoCheck(level.Update(0));
    if(!level.Animations().Diagnostics().empty()) {
        throw std::runtime_error(level.Animations().Diagnostics().front());
    }
    DemoCheck(level.Save(project / "Levels/Startup.level"));
}
}
