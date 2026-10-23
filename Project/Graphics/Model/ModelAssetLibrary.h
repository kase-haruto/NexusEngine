#pragma once
#include "ModelAssetData.h"
#include "ModelAssetReference.h"
#include "PrimitiveShape.h"
#include "Foundation/Error/Result.h"
#include <filesystem>
#include <map>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * ModelAssetLibrary
 * - 相対Asset参照をCPUモデルへ解決し、材質Variantを一つのUpload snapshotへ統合する
 * - GPU資源やEditorの選択・履歴は保持しない
 *---------------------------------------------------------------------------------------*/
class ModelAssetLibrary final {
public:
    struct Range { uint32_t begin = 0, end = 0, materialBegin = 0; };
    ModelAssetLibrary(std::filesystem::path root, const ModelAssetData& initial);
    /** \brief Root内の既存ファイルへ正規化する。外部やsymlink脱出を拒否する */
    Result<std::string> Reference(const std::filesystem::path& path) const;
    /** \brief 対応形式を検証しCPU cacheへロードする */
    Result<const ModelAssetData*> Model(const std::string& reference);
    Result<const std::vector<uint8_t>*> Texture(const std::string& reference);
    /** \brief MeshとBase Color画像の組から統合Node範囲を返す */
    Result<Range> Resolve(const std::string& mesh, const std::string& texture);
    /** Built-in shapes are generated once and share the normal material/upload path. */
    Result<Range> Resolve(PrimitiveShape shape, const std::string& mesh, const std::string& texture);
    const ModelAssetData& Snapshot() const { return aggregate_; }
    size_t Revision() const { return ranges_.size(); }
    const auto& Root() const { return root_; }
private:
    std::filesystem::path root_; //< 正規化Assetルート
    ModelAssetData aggregate_; //< Rendererへ渡す独立CPU snapshot
    std::map<std::string, ModelAssetData> models_; //< Sourceモデル。Variantによって変更しない
    std::map<std::string, std::vector<uint8_t>> textures_; //< Decode検証済み画像
    std::map<std::pair<std::string, std::string>, Range> ranges_; //< 永続参照から実行時範囲への対応
};
}
