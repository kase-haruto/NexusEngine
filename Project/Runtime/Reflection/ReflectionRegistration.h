#pragma once

namespace NexusEngine {
class ComponentReflection;
class TypeRegistry;
void RegisterGeneratedComponents(ComponentReflection& registry);
void RegisterGeneratedInspectorTypes(TypeRegistry& registry);

/*-----------------------------------------------------------------------------------------
 * ReflectionRegistration
 * - ホストが選択した型の登録手順を値として共有する
 * - 型やWorldを所有せず、Gameの具体型にも依存しない
 *---------------------------------------------------------------------------------------*/
struct ReflectionRegistration {
    void (*components)(ComponentReflection&) = RegisterGeneratedComponents; //< Level保存・復元の型集合
    void (*inspector)(TypeRegistry&) = RegisterGeneratedInspectorTypes; //< Detailsの型集合
};
}
