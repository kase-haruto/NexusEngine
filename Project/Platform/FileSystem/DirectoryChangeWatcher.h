#pragma once
#include "Foundation/Error/Result.h"
#include <filesystem>
#include <memory>
#include <vector>

namespace NexusEngine {
struct DirectoryChanges {
    std::vector<std::filesystem::path> paths;
    bool overflow = false;
};
/*-----------------------------------------------------------------------------------------
 * DirectoryChangeWatcher
 * - OSの非同期変更通知を保持し、所有Threadから非blockingで完了分だけを取り出す
 * - 再帰走査や監視Threadを作らず、Win32 handleとOVERLAPPEDをUIへ公開しない
 *---------------------------------------------------------------------------------------*/
class DirectoryChangeWatcher final {
  public:
    DirectoryChangeWatcher();
    ~DirectoryChangeWatcher();
    DirectoryChangeWatcher(const DirectoryChangeWatcher &) = delete;
    DirectoryChangeWatcher &operator=(const DirectoryChangeWatcher &) = delete;
    Result<void> Start(const std::filesystem::path &root, bool recursive = true);
    void Stop() noexcept;
    Result<DirectoryChanges> Poll();
    bool Active() const noexcept;
    size_t CompletionCount() const noexcept;

  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace NexusEngine
