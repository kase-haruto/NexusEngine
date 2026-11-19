#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include <span>
#include <functional>
#include "Foundation/Error/Result.h"

namespace NexusEngine {
enum class ContentPreview { None, Texture, Model };
/*-----------------------------------------------------------------------------------------
 * ContentItemRegistry
 * - 種別・検索名・Icon ID・Preview能力を登録する。UIに拡張子の分岐を持たせない
 * - Open / Import / Metadataなどの実処理は既存Workspaceと各Asset serviceが担当する
 *---------------------------------------------------------------------------------------*/
struct ContentItemAction {
    std::string labelKey, icon;
    std::function<Result<void>(const std::filesystem::path &)> execute;
    std::function<bool(const std::filesystem::path &)> enabled;
};
struct ContentItemType {
    std::string id, labelKey, icon;
    std::vector<std::string> extensions;
    ContentPreview preview = ContentPreview::None;
    bool source = false;
    std::function<void(const std::filesystem::path &)> open;
    std::vector<ContentItemAction> actions;
    bool moveAcrossFolders = false; //< 相対依存を持たない形式だけが明示的に有効化する
};
class ContentItemRegistry final {
  public:
    ContentItemRegistry();
    bool Register(ContentItemType type);
    ContentItemRegistry MetadataOnly() const;
    size_t Generation() const noexcept;
    const ContentItemType &Classify(const std::filesystem::path &path, bool directory) const;
    const ContentItemType &Find(std::string_view labelKey) const;
    std::span<const ContentItemType> Types() const;

  private:
    std::vector<ContentItemType> types_;
    size_t generation_ = 0;
};
} // namespace NexusEngine
