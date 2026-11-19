#pragma once
#include "ContentMetadata.h"
#include <map>
#include <future>
#include <stop_token>

namespace NexusEngine {
struct ContentGraphRoot {
    std::string id;
    std::filesystem::path path;
    bool readOnly = false;
};
struct ContentGraphSnapshot {
    std::map<std::filesystem::path, ContentMetadata> files;
    std::map<std::filesystem::path, std::vector<std::filesystem::path>> references;
    std::vector<std::string> warnings;
};
/*-----------------------------------------------------------------------------------------
 * ContentDependencyGraph
 * - Root単位の読取indexから依存と逆参照を構築する。明示要求時だけworkerを起動する
 * - workerはRootの値を所有し、ImGuiや編集中のDocumentへアクセスしない
 *---------------------------------------------------------------------------------------*/
class ContentDependencyGraph final {
  public:
    ContentDependencyGraph();
    ~ContentDependencyGraph();
    void Request(std::vector<ContentGraphRoot> roots);
    void SetSourceTypes(std::map<std::string, std::vector<std::filesystem::path>> types);
    void Invalidate();
    void Poll();
    bool Busy() const;
    bool Dirty() const;
    size_t Revision() const;
    std::shared_ptr<const ContentGraphSnapshot> Snapshot() const;
    const std::string &ErrorText() const;

  private:
    std::map<std::string, std::vector<std::filesystem::path>> sourceTypes_;
    std::future<Result<ContentGraphSnapshot>> task_;
    std::stop_source cancellation_;
    std::shared_ptr<const ContentGraphSnapshot> snapshot_;
    size_t revision_ = 0;
    bool dirty_ = true;
    std::string error_;
};
} // namespace NexusEngine
