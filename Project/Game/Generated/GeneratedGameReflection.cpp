// 自動生成ファイル。型定義側の NEXUS_REFLECT 宣言から再生成する。
#include "GeneratedGameReflection.h"
#include "Runtime/Reflection/ComponentReflection.h"
#include "Runtime/Reflection/ReflectedInspector.h"
namespace NexusEngine {
void RegisterGameComponents(ComponentReflection& registry) {
    registry.Register<NexusGame::MoveComponent>();
}
void RegisterGameInspectorTypes(TypeRegistry& registry) {
    RegisterReflectedInspector<NexusGame::MoveComponent>(registry);
}
}
