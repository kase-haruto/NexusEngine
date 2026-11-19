// 自動生成ファイル。型定義側の NEXUS_REFLECT 宣言から再生成する。
#include "GeneratedReflection.h"
#include "Runtime/Reflection/ComponentReflection.h"
#include "Runtime/Reflection/ReflectedInspector.h"
namespace NexusEngine {
void RegisterGeneratedComponents(ComponentReflection& registry) {
    registry.Register<AnimationPlayerComponent>();
    registry.Register<AnimatorComponent>();
    registry.Register<CameraComponent>();
    registry.Register<CameraEffectComponent>();
    registry.Register<ColliderComponent>();
    registry.Register<CurveBindingComponent>();
    registry.Register<DirectionalLightComponent>();
    registry.Register<ExponentialHeightFogComponent>();
    registry.Register<FogComponent>();
    registry.Register<HierarchyComponent>();
    registry.Register<KeyframeAnimationComponent>();
    registry.Register<NameComponent>();
    registry.Register<PersistentIdComponent>();
    registry.Register<PrimitiveRenderComponent>();
    registry.Register<RigidBodyComponent>();
    registry.Register<ScriptComponent>();
    registry.Register<SkeletalAnimationComponent>();
    registry.Register<SkyAtmosphereComponent>();
    registry.Register<SkyLightComponent>();
    registry.Register<SkyRendererComponent>();
    registry.Register<TimelinePlayerComponent>();
    registry.Register<TransformComponent>();
}
void RegisterGeneratedInspectorTypes(TypeRegistry& registry) {
    RegisterReflectedInspector<AnimationPlayerComponent>(registry, {"C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/AnimationPlayerComponent.h", "C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/AnimationPlayerComponent.cpp"}, {"", "Animation", "componentBrowser.description.animationPlayer", "details.Animation", "Engine", {"AnimationPlayerComponent"}});
    RegisterReflectedInspector<AnimatorComponent>(registry, {"C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/AnimatorComponent.h", "C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/AnimatorComponent.cpp"}, {"", "Animation", "componentBrowser.description.animator", "details.Animation", "Engine", {"AnimatorComponent"}});
    RegisterReflectedInspector<CameraComponent>(registry, {"C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/CameraComponent.h"}, {"", "Camera", "componentBrowser.description.camera", "details.Camera", "Engine", {"CameraComponent"}});
    RegisterReflectedInspector<CameraEffectComponent>(registry, {"C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/CameraEffectComponent.h", "C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/CameraEffectComponent.cpp"}, {"", "Camera", "componentBrowser.description.cameraEffect", "details.Camera", "Engine", {"CameraEffectComponent"}});
    RegisterReflectedInspector<ColliderComponent>(registry, {"C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/ColliderComponent.h"}, {"", "Physics", "componentBrowser.description.collider", "component.Physics", "Engine", {"ColliderComponent"}});
    RegisterReflectedInspector<CurveBindingComponent>(registry, {"C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/CurveBindingComponent.h", "C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/CurveBindingComponent.cpp"}, {"", "Animation", "componentBrowser.description.curveBinding", "details.Animation", "Engine", {"CurveBindingComponent"}});
    RegisterReflectedInspector<DirectionalLightComponent>(registry, {"C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/DirectionalLightComponent.h"}, {"", "Lighting", "componentBrowser.description.light", "details.Light", "Engine", {"DirectionalLightComponent"}});
    RegisterReflectedInspector<ExponentialHeightFogComponent>(registry, {"C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/ExponentialHeightFogComponent.h"}, {"", "Lighting", "componentBrowser.description.heightFog", "details.Fog", "Engine", {"ExponentialHeightFogComponent"}});
    RegisterReflectedInspector<FogComponent>(registry, {"C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/FogComponent.h"}, {"", "Lighting", "componentBrowser.description.fog", "details.Fog", "Engine", {"FogComponent"}});
    RegisterReflectedInspector<HierarchyComponent>(registry, {"C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/HierarchyComponent.h"}, {"", "Other", "componentBrowser.description.default", "component.Default", "Engine", {"HierarchyComponent"}});
    RegisterReflectedInspector<KeyframeAnimationComponent>(registry, {"C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/KeyframeAnimationComponent.h", "C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/KeyframeAnimationComponent.cpp"}, {"", "Animation", "componentBrowser.description.keyframe", "details.Animation", "Engine", {"KeyframeAnimationComponent"}});
    RegisterReflectedInspector<NameComponent>(registry, {"C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/NameComponent.h"}, {"", "Other", "componentBrowser.description.name", "details.Object", "Engine", {"NameComponent"}});
    RegisterReflectedInspector<PersistentIdComponent>(registry, {"C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/PersistentIdComponent.h"}, {"", "Other", "componentBrowser.description.default", "component.Default", "Engine", {"PersistentIdComponent"}});
    RegisterReflectedInspector<PrimitiveRenderComponent>(registry, {"C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/PrimitiveRenderComponent.h"}, {"", "Rendering", "componentBrowser.description.renderer", "details.Rendering", "Engine", {"PrimitiveRenderComponent"}});
    RegisterReflectedInspector<RigidBodyComponent>(registry, {"C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/RigidBodyComponent.h"}, {"", "Physics", "componentBrowser.description.rigidBody", "component.Physics", "Engine", {"RigidBodyComponent"}});
    RegisterReflectedInspector<ScriptComponent>(registry, {"C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/ScriptComponent.h"}, {"", "Gameplay", "componentBrowser.description.scriptContainer", "component.Script", "Engine", {"ScriptComponent"}});
    RegisterReflectedInspector<SkeletalAnimationComponent>(registry, {"C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/SkeletalAnimationComponent.h", "C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/SkeletalAnimationComponent.cpp"}, {"", "Animation", "componentBrowser.description.skeletal", "details.Animation", "Engine", {"SkeletalAnimationComponent"}});
    RegisterReflectedInspector<SkyAtmosphereComponent>(registry, {"C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/SkyAtmosphereComponent.h"}, {"", "Lighting", "componentBrowser.description.atmosphere", "details.Sky", "Engine", {"SkyAtmosphereComponent"}});
    RegisterReflectedInspector<SkyLightComponent>(registry, {"C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/SkyLightComponent.h"}, {"", "Lighting", "componentBrowser.description.skyLight", "details.Light", "Engine", {"SkyLightComponent"}});
    RegisterReflectedInspector<SkyRendererComponent>(registry, {"C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/SkyRendererComponent.h"}, {"", "Rendering", "componentBrowser.description.sky", "details.Sky", "Engine", {"SkyRendererComponent"}});
    RegisterReflectedInspector<TimelinePlayerComponent>(registry, {"C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/TimelinePlayerComponent.h", "C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/TimelinePlayerComponent.cpp"}, {"", "Animation", "componentBrowser.description.timeline", "details.Animation", "Engine", {"TimelinePlayerComponent"}});
    RegisterReflectedInspector<TransformComponent>(registry, {"C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/TransformComponent.h"}, {"", "Other", "componentBrowser.description.transform", "details.Object", "Engine", {"TransformComponent"}});
}
}
