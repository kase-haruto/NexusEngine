#include "AssetCatalog.h"
#include <algorithm>
#include <cctype>
#include "Platform/FileSystem/AssetIdentity.h"

namespace NexusEngine {
AssetCatalog::AssetCatalog(std::filesystem::path root) {
    std::error_code error;
    root_ = std::filesystem::weakly_canonical(root, error);
    if (error)
        root_ = std::move(root);
    current_ = root_;
}
std::string AssetCatalog::Utf8(const std::filesystem::path &path) {
    auto value = path.u8string();
    return {reinterpret_cast<const char *>(value.data()), value.size()};
}
Result<std::string> AssetCatalog::StableReference(const std::filesystem::path &file) const {
    return EnsureAssetIdentity(root_, file);
}
Result<void> AssetCatalog::Rename(const std::filesystem::path &source, const std::filesystem::path &destination) {
    auto renamed = RenameAssetWithIdentity(root_, source, destination);
    if (renamed) {
        cache_.clear();
        ++revision_;
    }
    return renamed;
}
bool AssetCatalog::Contains(const std::filesystem::path &path) const {
    auto relative = path.lexically_relative(root_);
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}
bool AssetCatalog::Navigate(const std::filesystem::path &path) {
    std::error_code error;
    auto resolved = std::filesystem::weakly_canonical(path, error);
    if (error || !Contains(resolved) || !std::filesystem::is_directory(resolved, error) || error)
        return false;
    current_ = std::move(resolved);
    return true;
}
std::vector<AssetEntry> AssetCatalog::List(const std::filesystem::path &path) const {
    std::vector<AssetEntry> result;
    std::error_code error;
    auto resolved = std::filesystem::weakly_canonical(path, error);
    if (error || !Contains(resolved))
        return result;
    std::filesystem::directory_iterator iterator(resolved, std::filesystem::directory_options::skip_permission_denied,
                                                 error),
        end;
    for (; !error && iterator != end; iterator.increment(error)) {
        if (iterator->path().filename() == ".gitkeep" || iterator->path().filename() == ".gitignore" ||
            iterator->path().filename() == ".nexus-trash")
            continue;
        auto canonical = std::filesystem::weakly_canonical(iterator->path(), error);
        if (error || !Contains(canonical)) {
            error.clear();
            continue;
        }
        bool directory = iterator->is_directory(error);
        if (error) {
            error.clear();
            continue;
        }
        std::string extension = Utf8(canonical.extension());
        auto filename = Utf8(canonical.filename());
        const auto lower = [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        };
        std::ranges::transform(extension, extension.begin(), lower);
        std::ranges::transform(filename, filename.begin(), lower);
        if (extension == ".nxmeta" || extension == ".nximport" || filename == ".nexusassets.json") {
            continue;
        }
        const auto &type = types_.Classify(canonical, directory);
        AssetEntry entry{canonical, Utf8(iterator->path().filename()), type.labelKey, directory};
        if (!directory) {
            entry.bytes = iterator->file_size(error);
            if (error) {
                entry.bytes = 0;
                error.clear();
            }
        }
        entry.modified = iterator->last_write_time(error);
        if (error) {
            error.clear();
        }
        result.push_back(std::move(entry));
    }
    std::ranges::sort(result, [](const auto &a, const auto &b) {
        return a.directory != b.directory ? a.directory : a.name < b.name;
    });
    return result;
}
std::shared_ptr<const std::vector<AssetEntry>> AssetCatalog::Snapshot(const std::filesystem::path &path) const {
    PollChanges();
    // 型の登録変更は明示的な変更通知として扱う。時間やidle frameでは列挙しない。
    if (typeGeneration_ != types_.Generation()) {
        typeGeneration_ = types_.Generation();
        cache_.clear();
        ++revision_;
    }
    const auto key = path.lexically_normal();
    if (const auto found = cache_.find(key); found != cache_.end()) {
        return found->second;
    }
    // 閲覧だけでcacheを期限切れにしない。変更はRefresh / 作成 / 移動から通知する。
    ++scanCount_;
    const auto started = std::chrono::steady_clock::now();
    auto entries = std::make_shared<const std::vector<AssetEntry>>(List(path));
    if (autoInvalidated_.erase(key) > 0 && automatic_) {
        const auto elapsed = std::chrono::steady_clock::now() - started;
        slowAutomaticScans_ = elapsed > std::chrono::milliseconds(16) ? slowAutomaticScans_ + 1 : 0;
        if (slowAutomaticScans_ >= 3) {
            watcher_.Stop();
            automatic_ = false;
            watchStatus_ =
                "Automatic refresh paused: three folder refreshes exceeded the 16 ms UI budget. Use Refresh.";
        }
    }
    cache_[key] = entries;
    return entries;
}
void AssetCatalog::Refresh() {
    autoInvalidated_.clear();
    cache_.clear();
    ++revision_;
}
size_t AssetCatalog::ScanCount() const noexcept {
    return scanCount_;
}
ContentItemRegistry &AssetCatalog::Types() {
    return types_;
}
const ContentItemRegistry &AssetCatalog::Types() const {
    return types_;
}
void AssetCatalog::SetAutomaticRefresh(bool enabled) {
    watcher_.Stop();
    automatic_ = false;
    watchStatus_.clear();
    changedFolders_.clear();
    autoInvalidated_.clear();
    slowAutomaticScans_ = 0;
    overflow_ = false;
    changesAt_ = {};
    lastWatchPoll_ = {};
    watchEvents_ = watchOverflows_ = 0;
    if (enabled) {
        const auto started = watcher_.Start(root_);
        if (started) {
            automatic_ = true;
            watchWindow_ = std::chrono::steady_clock::now();
            // 停止中の変更も、有効化という明示操作で一度取り込む。
            Refresh();
        } else {
            watchStatus_ = started.error().GetMessageText();
        }
    }
}
bool AssetCatalog::AutomaticRefresh() const noexcept {
    return automatic_;
}
size_t AssetCatalog::Revision() const noexcept {
    return revision_;
}
const std::string &AssetCatalog::WatchStatus() const noexcept {
    return watchStatus_;
}
void AssetCatalog::PollChanges() const {
    if (!automatic_) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now - lastWatchPoll_ < std::chrono::milliseconds(8)) {
        return;
    }
    lastWatchPoll_ = now;
    const auto changed = watcher_.Poll();
    if (!changed) {
        watchStatus_ = changed.error().GetMessageText();
        automatic_ = false;
        return;
    }
    if (now - watchWindow_ > std::chrono::seconds(1)) {
        watchWindow_ = now;
        watchEvents_ = 0;
        watchOverflows_ = 0;
    }
    for (const auto &path : changed->paths) {
        const auto relative = path.lexically_relative(root_);
        bool ignored = false;
        for (const auto &part : relative) {
            if (part == ".git" || part == ".vs" || part == "Build" || part == "Generated" || part == "obj" ||
                part == "bin" || part == ".nexus-trash") {
                ignored = true;
                break;
            }
        }
        if (ignored || (path.extension() == ".nxmeta" || path.extension() == ".nximport") ||
            path.filename() == ".nexusassets.json") {
            continue;
        }
        changedFolders_.insert(path.parent_path());
        if (changesAt_ == std::chrono::steady_clock::time_point{}) {
            changesAt_ = now;
        }
        ++watchEvents_;
    }
    overflow_ |= changed->overflow;
    if (changed->overflow) {
        ++watchOverflows_;
    }
    if (changed->overflow && changesAt_ == std::chrono::steady_clock::time_point{}) {
        changesAt_ = now;
    }
    // 大量のビルド出力などで通知が飽和したら、全走査へfallbackせず監視を止める。
    if (watchOverflows_ > 8 || watchEvents_ > 5000 || changedFolders_.size() > 1024) {
        watcher_.Stop();
        automatic_ = false;
        watchStatus_ =
            "Automatic refresh paused: event budget exceeded. Use Refresh or re-enable after the external operation.";
        changedFolders_.clear();
        return;
    }
    if (changesAt_ == std::chrono::steady_clock::time_point{} || now - changesAt_ < std::chrono::milliseconds(250)) {
        return;
    }
    if (overflow_) {
        for (const auto &[key, snapshot] : cache_) {
            static_cast<void>(snapshot);
            autoInvalidated_.insert(key);
        }
        cache_.clear();
    } else {
        for (auto entry = cache_.begin(); entry != cache_.end();) {
            const bool affected = std::ranges::any_of(changedFolders_, [&](const auto &folder) {
                const auto relative = entry->first.lexically_relative(folder);
                return entry->first == folder ||
                       (!relative.empty() && !relative.is_absolute() && *relative.begin() != "..");
            });
            if (affected) {
                autoInvalidated_.insert(entry->first);
                entry = cache_.erase(entry);
            } else {
                ++entry;
            }
        }
    }
    ++revision_;
    changedFolders_.clear();
    overflow_ = false;
    changesAt_ = {};
}

} // namespace NexusEngine
