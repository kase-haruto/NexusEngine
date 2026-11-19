#pragma once
#include "AssetCatalog.h"
#include <future>
#include <stop_token>
#include <atomic>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * ContentIndex
 * - 明示検索・変更通知後の要求だけで非同期の再帰indexを作る。idle時には走査しない
 * - 公開snapshotは不変、workerはコピーしたRegistryとpathだけを使用する
 *---------------------------------------------------------------------------------------*/
class ContentIndex final {
  public:
    ContentIndex();
    ~ContentIndex();
    void Request(std::filesystem::path root, ContentItemRegistry registry, bool metrics = false);
    void Invalidate();
    void Poll();
    bool Busy() const noexcept;
    bool Dirty() const noexcept;
    size_t Revision() const noexcept;
    const std::string &ErrorText() const noexcept;
    std::shared_ptr<const std::vector<AssetEntry>> Snapshot() const;
    static Result<std::vector<AssetEntry>> Build(const std::filesystem::path &root, const ContentItemRegistry &registry,
                                                 bool metrics, std::stop_token stop = {});

  private:
    std::future<Result<std::vector<AssetEntry>>> task_;
    std::stop_source cancellation_;
    std::shared_ptr<const std::vector<AssetEntry>> snapshot_;
    std::string error_;
    bool dirty_ = true, metrics_ = false;
    size_t revision_ = 0;
    std::chrono::steady_clock::time_point dirtyAt_{};
};
} // namespace NexusEngine
