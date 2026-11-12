#pragma once
#include <memory>

namespace NexusEngine {
class ComponentReflection;
class TypeRegistry;
class NativeComponentMetadata;
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
    std::shared_ptr<const NativeComponentMetadata> nativeComponents; //< 値だけのCatalog。DLL objectを所有しない
    /** \brief ホストの既存型と値Component格納型を同じLevel保存基盤へ登録する */
    void RegisterComponents(ComponentReflection& registry) const;
    /** \brief ホストとDLL由来の値Metadataを同じInspector registryへ登録する */
    void RegisterInspector(TypeRegistry& registry) const;
};
}
