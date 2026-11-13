// 自動生成ファイル。型定義側の NEXUS_REFLECT 宣言から再生成する。
#include "GeneratedReflection.h"
#include "Runtime/Reflection/ComponentReflection.h"
#include "Runtime/Reflection/ReflectedInspector.h"
namespace NexusEngine {
void RegisterGeneratedComponents(ComponentReflection& registry) {
    registry.Register<AnimationPlayerComponent>();
    registry.Register<CameraComponent>();
    registry.Register<ColliderComponent>();
    registry.Register<DirectionalLightComponent>();
    registry.Register<ExponentialHeightFogComponent>();
    registry.Register<FogComponent>();
    registry.Register<HierarchyComponent>();
    registry.Register<NameComponent>();
    registry.Register<PersistentIdComponent>();
    registry.Register<PrimitiveRenderComponent>();
    registry.Register<RigidBodyComponent>();
    registry.Register<ScriptComponent>();
    registry.Register<SkyAtmosphereComponent>();
    registry.Register<SkyLightComponent>();
    registry.Register<SkyRendererComponent>();
    registry.Register<TransformComponent>();
}
void RegisterGeneratedInspectorTypes(TypeRegistry& registry) {
    RegisterReflectedInspector<AnimationPlayerComponent>(registry, {"C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/AnimationPlayerComponent.h"});
    RegisterReflectedInspector<CameraComponent>(registry, {"C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/CameraComponent.h"});
    RegisterReflectedInspector<ColliderComponent>(registry, {"C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/ColliderComponent.h"});
    RegisterReflectedInspector<DirectionalLightComponent>(registry, {"C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/DirectionalLightComponent.h"});
    RegisterReflectedInspector<ExponentialHeightFogComponent>(registry, {"C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/ExponentialHeightFogComponent.h"});
    RegisterReflectedInspector<FogComponent>(registry, {"C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/FogComponent.h"});
    RegisterReflectedInspector<HierarchyComponent>(registry, {"C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/HierarchyComponent.h"});
    RegisterReflectedInspector<NameComponent>(registry, {"C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/NameComponent.h"});
    RegisterReflectedInspector<PersistentIdComponent>(registry, {"C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/PersistentIdComponent.h"});
    RegisterReflectedInspector<PrimitiveRenderComponent>(registry, {"C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/PrimitiveRenderComponent.h"});
    RegisterReflectedInspector<RigidBodyComponent>(registry, {"C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/RigidBodyComponent.h"});
    RegisterReflectedInspector<ScriptComponent>(registry, {"C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/ScriptComponent.h"});
    RegisterReflectedInspector<SkyAtmosphereComponent>(registry, {"C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/SkyAtmosphereComponent.h"});
    RegisterReflectedInspector<SkyLightComponent>(registry, {"C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/SkyLightComponent.h"});
    RegisterReflectedInspector<SkyRendererComponent>(registry, {"C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/SkyRendererComponent.h"});
    RegisterReflectedInspector<TransformComponent>(registry, {"C:/MyProject/NexusEngine/Project/Runtime/Scene/Components/TransformComponent.h"});
}
}
