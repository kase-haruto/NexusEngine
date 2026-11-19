#pragma once
#include "Editor/Gui/Panels/IEditorPanel.h"
#include "Editor/Services/ContentBrowserState.h"
#include "Editor/Services/ContentFileTransaction.h"
#include "Editor/Services/SourceFileActions.h"
#include "Editor/Services/ContentBrowserServices.h"
#include "Editor/Services/ContentImportService.h"
#include "Editor/Services/ContentMutationService.h"
#include <array>
#include <functional>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * ContentBrowserPanel
 * - 表示・操作要求だけを担当する。型・検索・履歴・画像とfilesystem処理は各serviceへ委譲
 * - パネル単位の状態を所有し、別Browserと移動先・選択を共有しない
 *---------------------------------------------------------------------------------------*/
class ContentBrowserPanel final : public IEditorPanel {
  public:
    ContentBrowserPanel(AssetCatalog &assets, std::function<void(const std::filesystem::path &)> open,
                        AssetCatalog *sources = nullptr, SourceCreationService *creation = nullptr,
                        EditorSettingsStore *settings = nullptr, AssetCatalog *levels = nullptr,
                        std::shared_ptr<ContentBrowserServices> services = {}, std::string id = "ContentBrowser");
    std::array<float, 2> InitialSize() const noexcept override;
    std::string_view Id() const noexcept override;
    std::string_view TitleKey() const noexcept override;
    void Draw(const EditorContext &context) override;
    void RegisterRoot(std::string labelKey, std::string icon, AssetCatalog &catalog);

  private:
    struct Root {
        std::string label, icon;
        AssetCatalog *catalog;
        std::filesystem::path current;
        std::string id;
        bool readOnly = false, source = false, engine = false, plugin = false;
    };
    bool Navigate(size_t root, const std::filesystem::path &path, bool history = true);
    void Persist();
    void RequestImport(const std::filesystem::path &reimport = {});
    void DrawImport(const EditorContext &context);
    void RequestMoves(std::vector<std::pair<std::filesystem::path, std::filesystem::path>> moves);
    void DrawMutation(const EditorContext &context);
    void FolderDrop(const EditorContext &context, const std::filesystem::path &folder);
    void DrawToolbar(const EditorContext &context);
    void DrawSources(const EditorContext &context);
    void DrawLibrary(const EditorContext &context);
    void DrawLibraryDialogs(const EditorContext &context);
    std::shared_ptr<const std::vector<AssetEntry>> Listing();
    void SyncRoots();
    void ChooseFolder(size_t root, const std::filesystem::path &path, bool toggle);

    void DrawEntryActions(const EditorContext &context, const AssetEntry &entry);
    void DrawAssetView(const EditorContext &context);
    void DrawDialogs(const EditorContext &context);
    void DrawCreateMenu(const EditorContext &context, const std::filesystem::path &directory);
    void RequestSourceClone(const std::filesystem::path &file);
    void RequestSource(bool cpp, const std::filesystem::path &hint);
    void DrawSourceCreation(const EditorContext &context);
    void Open(const std::filesystem::path &path);
    void RunBatch(const EditorContext &context, ContentFileOperation operation,
                  const std::filesystem::path &destination = {});
    SourceCreationService *creation_ = nullptr;
    std::vector<SourceCreationModule> sourceModules_;
    std::array<char, 65> sourceName_{};
    std::string sourceError_;
    std::array<char, 257> sourceNamespace_{}, sourceBase_{}, sourceBaseHeader_{}, sourceDirectory_{};
    int sourceKind_ = 0;
    std::filesystem::path sourceClone_;
    std::string sourceCloneClass_;
    bool waitingForCreation_ = false;
    bool sourceRequested_ = false, sourceCpp_ = false, sourceOpen_ = true;
    int sourceModule_ = 0;
    uint64_t sourceRevision_ = 0;
    std::shared_ptr<ContentBrowserServices> services_;
    std::string id_, titleKey_;
    std::vector<ContentLocation> folders_;
    std::string virtualView_, collectionId_, collectionRename_, searchRename_, collectionDelete_, searchDelete_;
    std::array<char, 256> libraryName_{};
    bool collectionCreate_ = false, searchCreate_ = false, sharedCollection_ = false;
    std::shared_ptr<const std::vector<AssetEntry>> merged_;
    std::vector<std::shared_ptr<const std::vector<AssetEntry>>> inputs_;
    std::string listingKey_;
    bool showEngine_ = false, showPlugins_ = true, recursive_ = false;
    ContentMutationService mutation_;
    bool mutationRequested_ = false;
    ContentImportService import_;
    ContentImportOptions importOptions_;
    std::vector<std::filesystem::path> importSources_;
    std::filesystem::path importDestination_;
    bool importRequested_ = false;
    AssetCatalog &assets_;
    AssetCatalog *sources_ = nullptr;
    EditorSettingsStore *settings_ = nullptr;
    std::vector<Root> roots_;
    size_t rootIndex_ = 0;
    ContentBrowserState state_;
    std::vector<AssetEntry> visible_;
    std::shared_ptr<const std::vector<AssetEntry>> visibleSnapshot_;
    std::string visibleKey_;
    std::function<void(const std::filesystem::path &)> open_;
    std::string settingsKey_, error_, searchError_;
    std::array<char, 2048> directPath_{}, renamePath_{}, folderName_{};
    std::filesystem::path renameSource_, folderParent_;
    std::optional<std::filesystem::path> favoriteRemoval_;
    bool renameRequested_ = false, folderRequested_ = false;
    bool archiveRequested_ = false, moveRequested_ = false;
    std::shared_ptr<size_t> fileRevision_;
    std::optional<std::string> attach_;
    std::optional<ContentLocation> pendingNavigate_;
};
} // namespace NexusEngine
