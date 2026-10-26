#pragma once
#include "ModelAssetData.h"
#include "ModelAssetReference.h"
#include "PrimitiveShape.h"
#include "Foundation/Error/Result.h"
#include <filesystem>
#include <map>
#include <memory>
#include <span>
#include <functional>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * ModelAssetLibrary
 * - 相対Asset参照をCPUモデルへ解決し、材質Variantを一つのUpload snapshotへ統合する
 * - GPU資源やEditorの選択・履歴は保持しない
 *---------------------------------------------------------------------------------------*/
class ModelAssetLibrary final {
public:
    struct Range { uint32_t begin = 0, end = 0, materialBegin = 0; };
    struct Request { PrimitiveShape shape; std::string mesh, texture; };
    ModelAssetLibrary(std::filesystem::path root, const ModelAssetData& initial);
    using ReloadStartup = std::function<Result<ModelAssetData>()>;
    ModelAssetLibrary(std::shared_ptr<ModelAssetData> initial, std::filesystem::path root, ReloadStartup reloadStartup = {});
    /** \brief Root内の既存ファイルへ正規化する。外部やsymlink脱出を拒否する */
    Result<std::string> Reference(const std::filesystem::path& path) const;
    /** \brief 対応形式を検証しCPU cacheへロードする */
    Result<const ModelAssetData*> Model(const std::string& reference);
    Result<const std::vector<uint8_t>*> Texture(const std::string& reference);
    /** \brief MeshとBase Color画像の組から統合Node範囲を返す */
    Result<Range> Resolve(const std::string& mesh, const std::string& texture);
    /** Active built-in shapes share the normal material/upload path and are regenerated after eviction. */
    Result<Range> Resolve(PrimitiveShape shape, const std::string& mesh, const std::string& texture);
    /** Call before resolving any frame ranges. Unused variants and source caches are released.
     * Previously returned source pointers are invalidated; shared upload snapshots stay valid. */
    void Retain(std::span<const Request> requests);
    const ModelAssetData& Snapshot() const { return *aggregate_; }
    std::shared_ptr<const ModelAssetData> SharedSnapshot() const { return aggregate_; }
    size_t Revision() const { return revision_; }
    const auto& Root() const { return root_; }
private:
    std::filesystem::path root_; //< 正規化Assetルート
    std::shared_ptr<ModelAssetData> aggregate_; //< Published snapshots share storage; changes use copy-on-write
    std::map<std::string, std::shared_ptr<const ModelAssetData>> models_; //< Immutable source models
    std::map<std::string, std::vector<uint8_t>> textures_; //< Decode検証済み画像
    std::map<std::pair<std::string, std::string>, Range> ranges_; //< 永続参照から実行時範囲への対応
    size_t revision_ = 1; //< Monotonic even when a replacement has the same number of variants
    ReloadStartup reloadStartup_; //< Allows the synthetic startup source to be evicted too
};
}
