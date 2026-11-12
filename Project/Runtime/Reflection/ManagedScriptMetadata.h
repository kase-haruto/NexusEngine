#pragma once
#include "TypeRegistry.h"
#include "Foundation/Error/Result.h"
#include <filesystem>
#include <memory>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * ManagedScriptMetadata
 * - ビルド時に出力された値Metadataを共通TypeRegistryへ接続する。CLRを起動しない
 * - Descriptorはimmutableな値Metadataだけを共有所有し、EntityやManaged objectを保持しない
 * - 新しい保存Versionを古いMetadataで編集させず、共通Descriptorへ失敗理由を渡す
 *---------------------------------------------------------------------------------------*/
class ManagedScriptMetadata final {
public:
    /** \brief 上限・重複・対応型を検証し、Editor所有Catalogを生成する */
    [[nodiscard]] static Result<std::shared_ptr<const ManagedScriptMetadata>> Load(const std::filesystem::path& file);
    /** \brief 既存IDとの衝突を検査して共通Reflectionへ登録する。Workspace初期化thread専用 */
    [[nodiscard]] bool Register(TypeRegistry& registry) const;
    /** \brief このCatalogが登録した型だけをWorkspaceの差替え候補から除く */
    void Unregister(TypeRegistry& registry) const;
private:
    std::vector<TypeDescriptor> types_; //< Nativeの値accessorだけを所有する
};
/** \brief Interop書込前に状態形式を検査する。失敗しても既存Component値を変更しない */
[[nodiscard]] bool ValidateScriptPropertyState(std::string_view text) noexcept;
}
