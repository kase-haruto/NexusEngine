#pragma once
#include "AssetCatalog.h"
#include "AssetDocumentRegistry.h"
#include "EditorHistory.h"
#include <future>
#include <stop_token>

namespace NexusEngine {
struct ContentImportOptions {
    bool dependencies = true;
    bool replace = false;
    std::filesystem::path primaryName;
    std::vector<std::filesystem::path> relativePaths;
};
struct ContentImportFile {
    std::filesystem::path source, target;
    std::string bytes, before, beforeHash, sourceHash;
    bool existed = false;
};
struct ContentImportPlan {
    std::filesystem::path root;
    std::vector<ContentImportFile> files;
    ContentImportOptions options;
};
// 大きな読込と依存の列挙はworkerで行い、公開前に既存fileとの競合を再検証する。
class ContentImportService final {
  public:
    ~ContentImportService();
    Result<void> Start(std::vector<std::filesystem::path> sources, std::filesystem::path root,
                       std::filesystem::path destination, ContentImportOptions options);
    Result<void> DuplicateFolders(std::filesystem::path root, std::vector<std::filesystem::path> folders);
    void Poll();
    void Cancel();
    bool Busy() const;
    const std::optional<ContentImportPlan> &Plan() const;
    const std::string &ErrorText() const;
    Result<void> Publish(AssetCatalog &catalog, const AssetDocumentRegistry &documents, EditorHistory &history,
                         std::shared_ptr<size_t> revision);
    static Result<ContentImportPlan> Prepare(std::span<const std::filesystem::path> sources,
                                             const std::filesystem::path &root,
                                             const std::filesystem::path &destination, ContentImportOptions options,
                                             std::stop_token stop = {});

  private:
    std::future<Result<ContentImportPlan>> task_;
    std::stop_source cancellation_;
    std::optional<ContentImportPlan> plan_;
    std::string error_;
};
} // namespace NexusEngine
