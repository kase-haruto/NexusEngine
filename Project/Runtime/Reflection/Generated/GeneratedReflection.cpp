// 自動生成ファイル。型定義側の NEXUS_REFLECT 宣言から再生成する。
#include "GeneratedReflection.h"
#include "Runtime/Reflection/ComponentReflection.h"
#include "Runtime/Reflection/ReflectedInspector.h"
namespace NexusEngine {
void RegisterGeneratedComponents(ComponentReflection& registry) {
    registry.Register<CameraComponent>();
    registry.Register<DirectionalLightComponent>();
    registry.Register<HierarchyComponent>();
    registry.Register<NameComponent>();
    registry.Register<PersistentIdComponent>();
    registry.Register<PrimitiveRenderComponent>();
    registry.Register<TransformComponent>();
}
void RegisterGeneratedInspectorTypes(TypeRegistry& registry) {
    RegisterReflectedInspector<CameraComponent>(registry);
    RegisterReflectedInspector<DirectionalLightComponent>(registry);
    RegisterReflectedInspector<HierarchyComponent>(registry);
    RegisterReflectedInspector<NameComponent>(registry);
    RegisterReflectedInspector<PersistentIdComponent>(registry);
    RegisterReflectedInspector<PrimitiveRenderComponent>(registry);
    RegisterReflectedInspector<TransformComponent>(registry);
}
}
