#pragma once
#include "Foundation/Error/Result.h"
#include <filesystem>
#include <functional>
#include <vector>
#include <span>

namespace NexusEngine {
struct AssetDocumentRegistration {
    std::string id, label;
    std::function<std::filesystem::path()> path;
    std::function<bool()> dirty;
    std::function<Result<void>()> save;
};
/*-----------------------------------------------------------------------------------------
 * AssetDocumentRegistry
 * - 実際の編集documentのdirty/save callbackを集約する。Browserは具体Panelを参照しない
 * - Save Allは失敗を集計し、保存できなかったdocumentをclean扱いにしない
 *---------------------------------------------------------------------------------------*/
class AssetDocumentRegistry final {
  public:
    bool Register(AssetDocumentRegistration document);
    void Clear();
    Result<void> SaveAll();
    Result<void> Save(const std::filesystem::path &path);
    bool AnyDirty() const;
    bool Dirty(const std::filesystem::path &path) const;
    std::span<const AssetDocumentRegistration> Documents() const;

  private:
    std::vector<AssetDocumentRegistration> documents_;
};
} // namespace NexusEngine
