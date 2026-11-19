#pragma once
#include "EditorSettings.h"
#include "Foundation/Error/Result.h"
#include <vector>
#include <string>
#include <map>
#include <span>

namespace NexusEngine {
struct ContentReference {
    std::string root, reference;
    bool operator==(const ContentReference &) const = default;
};
struct ContentCollection {
    std::string id, name;
    bool shared = false;
    std::vector<ContentReference> items;
};
struct ContentSavedSearch {
    std::string name, query;
    bool recursive = true;
    std::vector<ContentReference> scopes;
    std::vector<std::string> filters;
};
/*-----------------------------------------------------------------------------------------
 * ContentLibrary
 * - Local/Shared Collectionの参照、最近開いた項目、Saved Searchを保持する
 * - SharedだけをProject Configへ保存し、実Assetの移動・複製は行わない
 *---------------------------------------------------------------------------------------*/
class ContentLibrary final {
  public:
    void Initialize(EditorSettingsStore *settings, std::string projectKey, std::filesystem::path config);
    Result<void> Reload();
    Result<std::string> Create(std::string name, bool shared);
    Result<void> Rename(std::string_view id, std::string name);
    Result<void> Erase(std::string_view id);
    Result<void> Add(std::string_view id, std::span<const ContentReference> items);
    Result<void> Remove(std::string_view id, std::span<const ContentReference> items);
    Result<void> SaveSearch(ContentSavedSearch search);
    Result<void> RemoveSearch(std::string_view name);
    Result<void> Opened(ContentReference item);
    std::span<const ContentCollection> Collections() const;
    std::span<const ContentReference> Recent() const;
    std::span<const ContentSavedSearch> Searches() const;
    const ContentCollection *Find(std::string_view id) const;
    size_t Generation() const noexcept;

  private:
    Result<void> Store();
    EditorSettingsStore *settings_ = nullptr;
    std::string key_, sharedBytes_, sharedValue_, localValue_;
    std::filesystem::path sharedFile_;
    std::vector<ContentCollection> collections_;
    std::vector<ContentReference> recent_;
    std::vector<ContentSavedSearch> searches_;
    size_t generation_ = 0;
};
} // namespace NexusEngine
