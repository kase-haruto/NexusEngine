#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include "ContentItemRegistry.h"
#include <map>
#include <memory>
#include <chrono>
#include <set>
#include <optional>
#include "Platform/FileSystem/DirectoryChangeWatcher.h"
#include "Foundation/Error/Result.h"

namespace NexusEngine {
struct AssetEntry {
    std::filesystem::path path; //< 正規化済みパス
    std::string name, typeKey;  //< UTF-8名と種別翻訳キー
    bool directory = false;     //< フォルダか
    uintmax_t bytes = 0;
    std::filesystem::file_time_type modified{};
    std::optional<uint64_t> triangles;
    bool missing = false;
};
/*-----------------------------------------------------------------------------------------
 * AssetCatalog
 * - AssetDatabaseが未導入の段階でファイル一覧と安全な移動を提供する
 * - ImportやGPUロードは行わない。Browserはこの窓口だけを使用する
 *---------------------------------------------------------------------------------------*/
class AssetCatalog final {
  public:
    explicit AssetCatalog(std::filesystem::path root);
    /** \brief Assetルート内のフォルダへ移動する。外部symlinkは拒否する */
    bool Navigate(const std::filesystem::path &path);
    /** \brief 一つのフォルダを上限付きで列挙する */
    std::vector<AssetEntry> List(const std::filesystem::path &path) const;
    /** \brief 明示Refreshまで同じsnapshotを保持する。時刻による再走査は行わない */
    std::shared_ptr<const std::vector<AssetEntry>> Snapshot(const std::filesystem::path &path) const;
    void Refresh();
    void SetAutomaticRefresh(bool enabled);
    bool AutomaticRefresh() const noexcept;
    void PollChanges() const;
    size_t Revision() const noexcept;
    const std::string &WatchStatus() const noexcept;

    size_t ScanCount() const noexcept;
    ContentItemRegistry &Types();
    const ContentItemRegistry &Types() const;
    const auto &Root() const {
        return root_;
    }
    const auto &Current() const {
        return current_;
    }
    /** \brief Windows pathをUI用UTF-8へ変換する */
    static std::string Utf8(const std::filesystem::path &path);
    /** \brief sidecarを発行し、改名に追従する参照を返す。 */
    Result<std::string> StableReference(const std::filesystem::path &file) const;
    Result<void> Rename(const std::filesystem::path &source, const std::filesystem::path &destination);

  private:
    bool Contains(const std::filesystem::path &path) const;
    std::filesystem::path root_, current_; //< 正規化した範囲
    ContentItemRegistry types_;
    mutable DirectoryChangeWatcher watcher_;
    mutable std::set<std::filesystem::path> changedFolders_, autoInvalidated_;
    mutable size_t slowAutomaticScans_ = 0;
    mutable std::chrono::steady_clock::time_point changesAt_{}, watchWindow_{}, lastWatchPoll_{};
    mutable size_t revision_ = 0, watchEvents_ = 0, watchOverflows_ = 0;
    mutable bool automatic_ = false, overflow_ = false;
    mutable std::string watchStatus_;

    mutable size_t scanCount_ = 0;
    mutable size_t typeGeneration_ = 0;
    mutable std::map<std::filesystem::path, std::shared_ptr<const std::vector<AssetEntry>>>
        cache_; //< Refreshまで退役させず再走査を防ぐ
};
} // namespace NexusEngine
