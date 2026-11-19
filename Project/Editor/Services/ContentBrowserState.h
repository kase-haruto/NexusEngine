#pragma once
#include "AssetCatalog.h"
#include <set>
#include <optional>
#include <span>

namespace NexusEngine {
class EditorSettingsStore;
struct ContentLocation {
    size_t root = 0;
    std::filesystem::path path;
    bool operator==(const ContentLocation &) const = default;
};
/*-----------------------------------------------------------------------------------------
 * ContentBrowserNavigationHistory
 * - rootを含む移動履歴。ファイル走査・ImGui・Workspaceから独立した値状態
 *---------------------------------------------------------------------------------------*/
class ContentBrowserNavigationHistory final {
  public:
    void Visit(ContentLocation location);
    std::optional<ContentLocation> Back();
    std::optional<ContentLocation> Forward();
    bool CanBack() const;
    bool CanForward() const;

  private:
    std::vector<ContentLocation> locations_;
    size_t cursor_ = 0;
};
enum class ContentViewMode { Grid, List, Details };
enum class ContentSort { Name, Type, Size, Modified };
struct ContentSearchTerm {
    std::string field, value;
};
struct ContentSearchQuery {
    std::vector<ContentSearchTerm> terms;
    std::string error;
};
class ContentSearchParser final {
  public:
    static ContentSearchQuery Parse(std::string_view text);
    static bool Matches(const ContentSearchQuery &query, const AssetEntry &entry, const ContentItemRegistry &registry);
};
/*-----------------------------------------------------------------------------------------
 * ContentBrowserState
 * - Browser単位の検索・Filter・順序・選択を保持し、Catalogの不変snapshotを評価する
 * - User設定だけを保存し、ProjectのAsset / moduleへ表示状態を書き込まない
 *---------------------------------------------------------------------------------------*/
class ContentBrowserState final {
  public:
    ContentBrowserNavigationHistory history;
    std::string search, folderSearch;
    std::set<std::string> filters, hiddenTypes;
    std::vector<std::filesystem::path> favorites;
    std::vector<std::filesystem::path> selection;
    ContentViewMode view = ContentViewMode::List;
    ContentSort sort = ContentSort::Name;
    bool descending = false, folderFirst = true, showSources = true, showFolders = true, showSourceFiles = true;
    float thumbnailSize = 80;
    std::vector<AssetEntry> Visible(std::span<const AssetEntry> entries, const ContentItemRegistry &registry) const;
    void Select(std::span<const AssetEntry> visible, size_t index, bool ctrl, bool shift);
    bool Selected(const std::filesystem::path &path) const;
    void ToggleFavorite(const std::filesystem::path &path);
    void Load(EditorSettingsStore *settings, const std::string &key);
    bool Save(EditorSettingsStore *settings, const std::string &key) const;

  private:
    std::filesystem::path anchor_;
};
} // namespace NexusEngine
