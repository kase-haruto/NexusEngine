// 自動生成ファイル。型定義側の NEXUS_REFLECT 宣言から再生成する。
#pragma once
#include "Foundation/Reflection/ReflectionValue.h"
#include "Runtime/Reflection/EngineGenerated/GeneratedReflection.h"
#include "Game/Components/MoveComponent.h"
namespace NexusEngine {
template<> struct ReflectionTraits<NexusGame::MoveComponent> {
    static constexpr const char* Id = "Game.Move";
    static constexpr const char* Label = "component.gameMove";
    template<class Visitor> static void Visit(Visitor&& visitor) {
        visitor("speed", &NexusGame::MoveComponent::speed);
    }
};
}
namespace NexusEngine { class ComponentReflection; class TypeRegistry;
void RegisterGameComponents(ComponentReflection& registry);
void RegisterGameInspectorTypes(TypeRegistry& registry);
}
