#pragma once
#include "TypeRegistry.h"
#include "Foundation/Error/Result.h"
#include "Runtime/Gameplay/GameModuleLoader.h"
#include <memory>
#include <span>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * NativeComponentMetadata
 * - Game DLLの宣言からコピーした値Metadataを既存TypeRegistryへ接続する
 * - DLL handle / 関数参照 / Native objectを保持せず、EditorとRuntimeが同じCatalogを共有する
 * - 実体の値はSceneのGameComponentDataが所有し、DLL Unload後もLevel保存を可能にする
 *---------------------------------------------------------------------------------------*/
class NativeComponentMetadata final {
public:
    /** \brief Module identityを検証し、ゲーム開始前に任意Metadata exportを読み取る */
    static Result<std::shared_ptr<const NativeComponentMetadata>> Inspect(std::span<const NativeGameModuleSettings> modules);
    /** \brief コピー済みJSONの純粋な検証経路。DLLを実行せずCatalogを構築できる */
    static Result<std::shared_ptr<const NativeComponentMetadata>> Parse(std::string_view text, const std::filesystem::path& sourceDirectory = {});
    /** \brief 衝突を事前検査後に共通Property registryへ登録する */
    bool Register(TypeRegistry& registry) const;
    /** \brief 値ComponentとしてこのCatalogが公開する安定IDか返す */
    bool Contains(std::string_view id) const noexcept;
    /** \brief Schedulerが値Storageの依存宣言へ解決するためのID一覧を返す */
    const std::vector<TypeDescriptor>& Types() const noexcept { return types_; }
    /** \brief Editor開始時とPlay時のModule宣言が同じか検査する。値Catalogの暗黙差替えを禁止する */
    bool SameModules(const NativeComponentMetadata& other) const noexcept { return moduleSchemas_ == other.moduleSchemas_; }
    /** \brief 実際にロードするDLLへ照合する正規化JSON。空は任意exportなしを示す */
    std::string ModuleSchema(std::string_view module) const;
private:
    std::vector<TypeDescriptor> types_; //< Engineコードとimmutable値Metadataだけを捕捉するaccessor
    std::vector<std::string> names_; //< Module間の正規ID / Alias衝突を拒否する値コピー
    std::string schema_; //< 単独Parseで検証済みの正規化JSON
    std::map<std::string, std::string, std::less<>> moduleSchemas_; //< inspectionと実ロードの世代混在を拒否する
};
}
