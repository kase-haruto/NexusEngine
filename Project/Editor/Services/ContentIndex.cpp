#include "ContentIndex.h"
#include "ContentMetadata.h"
#include "Platform/FileSystem/AtomicFile.h"
#include <algorithm>
#include <cctype>

namespace NexusEngine {
ContentIndex::ContentIndex() : snapshot_(std::make_shared<const std::vector<AssetEntry>>()) {
}
ContentIndex::~ContentIndex() {
    cancellation_.request_stop();
    if (task_.valid()) {
        task_.wait();
    }
}
void ContentIndex::Request(std::filesystem::path root, ContentItemRegistry registry, bool metrics) {
    if (metrics && !metrics_) {
        metrics_ = true;
        dirty_ = true;
    }
    Poll();
    if (task_.valid() || !dirty_ || std::chrono::steady_clock::now() - dirtyAt_ < std::chrono::milliseconds(750)) {
        return;
    }
    dirty_ = false;
    error_.clear();
    cancellation_ = std::stop_source{};
    const auto stop = cancellation_.get_token();
    task_ = std::async(std::launch::async,
                       [root = std::move(root), registry = registry.MetadataOnly(), metrics = metrics_, stop] {
                           return Build(root, registry, metrics, stop);
                       });
}
void ContentIndex::Invalidate() {
    dirty_ = true;
    dirtyAt_ = std::chrono::steady_clock::now();
}
void ContentIndex::Poll() {
    if (!task_.valid() || task_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }
    try {
        auto result = task_.get();
        if (result) {
            snapshot_ = std::make_shared<const std::vector<AssetEntry>>(std::move(*result));
            ++revision_;
        } else {
            error_ = result.error().GetMessageText();
        }
    } catch (const std::exception &error) {
        error_ = error.what();
    }
}
bool ContentIndex::Busy() const noexcept {
    return task_.valid();
}
bool ContentIndex::Dirty() const noexcept {
    return dirty_;
}
size_t ContentIndex::Revision() const noexcept {
    return revision_;
}
const std::string &ContentIndex::ErrorText() const noexcept {
    return error_;
}
std::shared_ptr<const std::vector<AssetEntry>> ContentIndex::Snapshot() const {
    return snapshot_;
}
Result<std::vector<AssetEntry>> ContentIndex::Build(const std::filesystem::path &root,
                                                    const ContentItemRegistry &registry, bool metrics,
                                                    std::stop_token stop) {
    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(
        root, std::filesystem::directory_options::skip_permission_denied, error),
        end;
    if (error) {
        return std::unexpected(Error(ErrorCategory::FileSystem, 294, error.message()));
    }
    std::vector<AssetEntry> entries;
    while (iterator != end && !error && !stop.stop_requested()) {
        const auto path = iterator->path();
        const auto name = path.filename();
        auto internalName = AssetCatalog::Utf8(name), extension = AssetCatalog::Utf8(path.extension());
        const auto lower = [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        };
        std::ranges::transform(internalName, internalName.begin(), lower);
        std::ranges::transform(extension, extension.begin(), lower);
        const auto status = iterator->symlink_status(error);
        if (error) {
            error.clear();
            iterator.increment(error);
            continue;
        }
        const bool directory = std::filesystem::is_directory(status);
        const bool skip = std::filesystem::is_symlink(status) || internalName == ".git" || internalName == ".vs" ||
                          internalName == ".nexus-trash" || internalName == "build" || internalName == "generated" ||
                          internalName == "obj" || internalName == "bin";
        if (skip) {
            if (directory) {
                iterator.disable_recursion_pending();
            }
            iterator.increment(error);
            continue;
        }
        const auto canonical = ResolveFileWithinRoot(root, AssetCatalog::Utf8(path.lexically_relative(root)));
        if (!canonical) {
            if (directory) {
                iterator.disable_recursion_pending();
            }
            iterator.increment(error);
            continue;
        }
        if (internalName != ".nexusassets.json" && extension != ".nxmeta" && extension != ".nximport" &&
            name != ".gitignore" && name != ".gitkeep") {
            const auto &type = registry.Classify(path, directory);
            AssetEntry entry{*canonical, AssetCatalog::Utf8(name), type.labelKey, directory};
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
            if (metrics && type.id == "Model") {
                const auto metadata = ContentMetadataProvider::Read(path, root, false, stop);
                if (metadata) {
                    entry.triangles = metadata->triangles;
                }
            }
            entries.push_back(std::move(entry));
            if (entries.size() >= 100000) {
                return std::unexpected(
                    Error(ErrorCategory::Resource, 294,
                          "Recursive index limit exceeded (100000 items). Narrow the search roots."));
            }
        }
        iterator.increment(error);
    }
    if (stop.stop_requested()) {
        return std::unexpected(Error(ErrorCategory::Resource, 294, "Index cancelled."));
    }
    return entries;
}
} // namespace NexusEngine
