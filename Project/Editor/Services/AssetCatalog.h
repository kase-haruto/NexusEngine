#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include <chrono>
#include <map>
#include <memory>

namespace NexusEngine {
struct AssetEntry {
    std::filesystem::path path; //< 正規化済みパス
    std::string name, typeKey; //< UTF-8名と種別翻訳キー
    bool directory = false; //< フォルダか
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
    bool Navigate(const std::filesystem::path& path);
    /** \brief 一つのフォルダを上限付きで列挙する */
    std::vector<AssetEntry> List(const std::filesystem::path& path) const;
    /** \brief 2秒以内の同じフォルダは再走査しない。snapshotはcache退役後も有効 */
    std::shared_ptr<const std::vector<AssetEntry>> Snapshot(const std::filesystem::path& path) const;
    void Refresh() { cache_.clear(); }
    const auto& Root() const { return root_; }
    const auto& Current() const { return current_; }
    /** \brief Windows pathをUI用UTF-8へ変換する */
    static std::string Utf8(const std::filesystem::path& path);
private:
    bool Contains(const std::filesystem::path& path) const;
    std::filesystem::path root_, current_; //< 正規化した範囲
    struct Cached { std::shared_ptr<const std::vector<AssetEntry>> entries; std::chrono::steady_clock::time_point expires; };
    mutable std::map<std::filesystem::path, Cached> cache_; //< 最大128フォルダ
};
}
