#pragma once
#include "ContentLibrary.h"
#include "ContentIndex.h"
#include "ContentDependencyGraph.h"
#include "AssetDocumentRegistry.h"

namespace NexusEngine {
class TypeRegistry;
struct ContentBrowserRoot {
    std::string id, label, icon;
    AssetCatalog *catalog = nullptr;
    bool readOnly = false, source = false, engine = false, plugin = false;
    std::shared_ptr<AssetCatalog> owner;
    std::shared_ptr<ContentIndex> index;
    size_t observedRevision = 0;
};
struct ContentImportOrigin {
    std::filesystem::path source;
    std::string hash;
    bool dependencies = true;
};
enum class ContentInspectorMode { Metadata, Dependencies, References };
/*-----------------------------------------------------------------------------------------
 * ContentBrowserServices
 * - Workspace共有のRoot / Library / Document / Metadataを保持する
 * - 移動先・検索・選択範囲はBrowser側に置き、複数windowの状態を混ぜない
 *---------------------------------------------------------------------------------------*/
class ContentBrowserServices final {
  public:
    EditorSettingsStore *settings = nullptr; //< Workspaceより短い借用。OriginはUser設定だけへ保存
    std::optional<ContentImportOrigin> Origin(const ContentReference &reference) const;
    Result<void> RememberOrigins(std::span<const std::pair<ContentReference, ContentImportOrigin>> origins);
    std::shared_ptr<size_t> fileRevision = std::make_shared<size_t>(0); //< 全Browser共通のfile Undo/Redo通知
    ContentLibrary library;
    AssetDocumentRegistry documents;
    ContentDependencyGraph dependencies;
    ContentMetadataRequest metadata;
    std::function<void()> showInspector, contentChanged;
    std::vector<ContentBrowserRoot> roots;
    void AddRoot(std::string id, std::string label, std::string icon, AssetCatalog &catalog, bool readOnly = false,
                 bool source = false);
    void AddOwnedRoot(std::string id, std::string label, std::string icon, std::filesystem::path path, bool readOnly,
                      bool source, bool engine, bool plugin);
    std::optional<size_t> RootFor(const std::filesystem::path &file) const;
    Result<ContentReference> Reference(const std::filesystem::path &file);
    Result<std::filesystem::path> Resolve(const ContentReference &reference) const;
    std::vector<ContentGraphRoot> GraphRoots() const;
    void UpdateSourceTypes(const TypeRegistry &types, const std::filesystem::path &sourceRoot);
    void Poll();
    void Select(std::vector<std::filesystem::path> files, size_t sceneRevision,
                ContentInspectorMode mode = ContentInspectorMode::Metadata);
    void ClearSelection();
    bool SelectionActive(size_t sceneRevision) const;
    std::span<const std::filesystem::path> Selected() const;
    ContentInspectorMode InspectorMode() const;

  private:
    mutable std::map<std::string, std::pair<std::string, std::optional<ContentImportOrigin>>> origins_;
    std::vector<std::filesystem::path> selection_;
    size_t sceneRevision_ = 0, observedFileRevision_ = 0;
    uint64_t typeGeneration_ = UINT64_MAX;
    ContentInspectorMode mode_ = ContentInspectorMode::Metadata;
};
} // namespace NexusEngine
