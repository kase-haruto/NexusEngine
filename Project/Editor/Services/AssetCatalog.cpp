#include "AssetCatalog.h"
#include <algorithm>
#include <cctype>

namespace NexusEngine {
AssetCatalog::AssetCatalog(std::filesystem::path root) {
    std::error_code error;
    root_ = std::filesystem::weakly_canonical(root, error);
    if(error) root_ = std::move(root);
    current_ = root_;
}
std::string AssetCatalog::Utf8(const std::filesystem::path& path) {
    auto value = path.u8string(); return {reinterpret_cast<const char*>(value.data()), value.size()};
}
bool AssetCatalog::Contains(const std::filesystem::path& path) const {
    auto relative = path.lexically_relative(root_);
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}
bool AssetCatalog::Navigate(const std::filesystem::path& path) {
    std::error_code error;
    auto resolved = std::filesystem::weakly_canonical(path, error);
    if(error || !Contains(resolved) || !std::filesystem::is_directory(resolved, error) || error) return false;
    current_ = std::move(resolved); return true;
}
std::vector<AssetEntry> AssetCatalog::List(const std::filesystem::path& path) const {
    std::vector<AssetEntry> result;
    std::error_code error;
    auto resolved = std::filesystem::weakly_canonical(path, error);
    if(error || !Contains(resolved)) return result;
    std::filesystem::directory_iterator iterator(resolved, std::filesystem::directory_options::skip_permission_denied, error), end;
    for(; !error && iterator != end; iterator.increment(error)) {
        auto canonical = std::filesystem::weakly_canonical(iterator->path(), error);
        if(error || !Contains(canonical)) { error.clear(); continue; }
        bool directory = iterator->is_directory(error);
        if(error) { error.clear(); continue; }
        std::string extension = Utf8(canonical.extension());
        std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::string type = directory ? "asset.folder" : "asset.file";
        if(extension == ".gltf" || extension == ".glb" || extension == ".obj") type = "asset.model";
        if(extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp" || extension == ".tif" || extension == ".tiff") type = "asset.texture";
        if(extension == ".json" && canonical.parent_path().filename() == "Levels") type = "asset.level";
        result.push_back({canonical, Utf8(iterator->path().filename()), type, directory});
    }
    std::ranges::sort(result, [](const auto& a, const auto& b) { return a.directory != b.directory ? a.directory : a.name < b.name; });
    return result;
}
std::shared_ptr<const std::vector<AssetEntry>> AssetCatalog::Snapshot(const std::filesystem::path& path) const {
    const auto key = path.lexically_normal();
    const auto now = std::chrono::steady_clock::now();
    if(auto entry = cache_.find(key); entry != cache_.end() && entry->second.expires > now) return entry->second.entries;
    auto entries = std::make_shared<const std::vector<AssetEntry>>(List(path));
    if(cache_.size() == 128 && !cache_.contains(key)) {
        auto oldest = std::min_element(cache_.begin(),cache_.end(),[](const auto& a, const auto& b) { return a.second.expires < b.second.expires; });
        cache_.erase(oldest);
    }
    cache_[key] = {entries, now+std::chrono::seconds(2)}; return entries;
}
}
