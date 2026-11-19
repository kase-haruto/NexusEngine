#pragma once
#include "AssetCatalog.h"
#include "AssetDocumentRegistry.h"
#include "EditorHistory.h"
#include <map>
#include <future>
#include <stop_token>

namespace NexusEngine {
struct ContentMutationFile {
    std::filesystem::path beforePath, afterPath;
    std::string before, after;
    bool rewritten = false;
    uintmax_t bytes = 0;
    std::filesystem::file_time_type stamp{};
};
struct ContentMutationPlan {
    std::filesystem::path root;
    std::vector<ContentMutationFile> files;
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> folders;
};
// 変更前後の参照を準備し、folder/model/sourceの改名を一つの履歴へ公開する。
class ContentMutationService final {
  public:
    ~ContentMutationService();
    Result<void> Start(std::filesystem::path root,
                       std::vector<std::pair<std::filesystem::path, std::filesystem::path>> moves);
    void Poll();
    void Cancel();
    bool Busy() const;
    const std::optional<ContentMutationPlan> &Plan() const;
    const std::string &ErrorText() const;
    Result<void> Publish(const AssetDocumentRegistry &documents, EditorHistory &history,
                         std::shared_ptr<size_t> revision);
    static Result<ContentMutationPlan>
    Prepare(const std::filesystem::path &root,
            std::span<const std::pair<std::filesystem::path, std::filesystem::path>> moves, std::stop_token stop = {});

  private:
    std::future<Result<ContentMutationPlan>> task_;
    std::stop_source cancellation_;
    std::optional<ContentMutationPlan> plan_;
    std::string error_;
};
} // namespace NexusEngine
