#include "ContentMutationService.h"
#include "ContentIndex.h"
#include "ContentMetadata.h"
#include "Platform/FileSystem/AtomicFile.h"
#include "Platform/FileSystem/AssetIdentity.h"
#include "ThirdParty/nlohmann/json.hpp"
#include <algorithm>
#include <set>
#include <cstring>
#include <cctype>

namespace NexusEngine {
namespace {
Error Failed(std::string message) {
    return Error(ErrorCategory::Resource, 302, std::move(message));
}
bool Inside(const std::filesystem::path &file, const std::filesystem::path &root) {
    const auto relative = file.lexically_relative(root);
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}
void Replace(std::string &text, const std::string &from, const std::string &to) {
    if (from.empty() || from == to) {
        return;
    }
    size_t offset = 0;
    while ((offset = text.find(from, offset)) != std::string::npos) {
        text.replace(offset, from.size(), to);
        offset += to.size();
    }
}
std::string Extension(const std::filesystem::path &path) {
    auto extension = AssetCatalog::Utf8(path.extension());
    std::ranges::transform(extension, extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension;
}
std::string Relative(const std::filesystem::path &file, const std::filesystem::path &base) {
    const auto value = file.lexically_relative(base).generic_u8string();
    return {reinterpret_cast<const char *>(value.data()), value.size()};
}
// 文字列境界のある参照だけを書き換える。識別子や保存済みのasset://IDは維持する。
Result<std::string> Rewrite(const std::filesystem::path &path, std::string bytes,
                            const std::map<std::string, std::string> &replacements) {
    const auto extension = Extension(path);
    bool glb = extension == ".glb";
    uint32_t jsonBytes = 0;
    std::string text = bytes;
    if (glb) {
        if (bytes.size() < 20) {
            return std::unexpected(Failed("Invalid GLB."));
        }
        std::memcpy(&jsonBytes, bytes.data() + 12, 4);
        if (jsonBytes > 16 * 1024 * 1024 || size_t(jsonBytes) + 20 > bytes.size()) {
            return std::unexpected(Failed("GLB JSON budget exceeded."));
        }
        text = bytes.substr(20, jsonBytes);
    }
    for (const auto &[from, to] : replacements) {
        const auto encodedFrom = nlohmann::json(from).dump(), encodedTo = nlohmann::json(to).dump();
        Replace(text, encodedFrom, encodedTo);
        // Native metadataのJSONはC++ literal内に一段escapeして格納される。
        Replace(text, nlohmann::json(encodedFrom).dump().substr(1, nlohmann::json(encodedFrom).dump().size() - 2),
                nlohmann::json(encodedTo).dump().substr(1, nlohmann::json(encodedTo).dump().size() - 2));
        if (extension == ".obj" || extension == ".mtl") {
            Replace(text, " " + from + "\n", " " + to + "\n");
            Replace(text, " " + from + "\r\n", " " + to + "\r\n");
            if (text.ends_with(" " + from)) {
                text.replace(text.size() - from.size(), from.size(), to);
            }
        }
    }
    if (!glb) {
        return text;
    }
    while (text.size() % 4) {
        text += ' ';
    }
    std::string result = bytes.substr(0, 20) + text + bytes.substr(20 + jsonBytes);
    const auto length = static_cast<uint32_t>(result.size()), chunk = static_cast<uint32_t>(text.size());
    std::memcpy(result.data() + 8, &length, 4);
    std::memcpy(result.data() + 12, &chunk, 4);
    return result;
}
bool Apply(ContentMutationPlan &plan, bool forward) {
    std::error_code error;
    for (const auto &file : plan.files) {
        const auto &from = forward ? file.beforePath : file.afterPath;
        const auto &to = forward ? file.afterPath : file.beforePath;
        if (std::filesystem::file_size(from, error) != file.bytes || error ||
            std::filesystem::last_write_time(from, error) != file.stamp || error) {
            return false;
        }
        if (from != to && (std::filesystem::exists(to, error) || error ||
                           std::filesystem::exists(std::filesystem::path(to.wstring() + L".nxmeta"), error))) {
            return false;
        }
        if (file.rewritten) {
            const auto current = ContentMetadataProvider::Text(from, 128 * 1024 * 1024);
            if (!current || *current != (forward ? file.before : file.after)) {
                return false;
            }
        }
    }
    // 新しいfolderだけを先に準備する。既存fileを上書きするmoveは許可しない。
    for (const auto &folder : plan.folders) {
        const auto &to = forward ? folder.second : folder.first;
        std::filesystem::create_directories(to, error);
        if (error) {
            return false;
        }
    }
    size_t moved = 0, written = 0;
    for (const auto &file : plan.files) {
        const auto &from = forward ? file.beforePath : file.afterPath;
        const auto &to = forward ? file.afterPath : file.beforePath;
        if (from != to) {
            std::filesystem::create_directories(to.parent_path(), error);
            if (error || !RenameAssetWithIdentity(plan.root, from, to)) {
                break;
            }
        }
        ++moved;
    }
    if (moved == plan.files.size()) {
        for (const auto &file : plan.files) {
            const auto &to = forward ? file.afterPath : file.beforePath;
            if (file.rewritten && !WriteFileAtomically(to, forward ? file.after : file.before)) {
                break;
            }
            ++written;
        }
    }
    const bool success = moved == plan.files.size() && written == plan.files.size();
    if (!success) {
        while (written > 0) {
            const auto &file = plan.files[--written];
            if (file.rewritten) {
                static_cast<void>(WriteFileAtomically(forward ? file.afterPath : file.beforePath,
                                                      forward ? file.before : file.after));
            }
        }
        while (moved > 0) {
            const auto &file = plan.files[--moved];
            if (file.beforePath != file.afterPath) {
                static_cast<void>(RenameAssetWithIdentity(plan.root, forward ? file.afterPath : file.beforePath,
                                                          forward ? file.beforePath : file.afterPath));
            }
        }
    }
    // undo/redo後の競合検出に、今回公開したfileのsignatureを保存する。
    for (auto &file : plan.files) {
        const auto &path = (success == forward) ? file.afterPath : file.beforePath;
        file.bytes = std::filesystem::file_size(path, error);
        file.stamp = std::filesystem::last_write_time(path, error);
    }
    if (success) {
        for (auto folder = plan.folders.rbegin(); folder != plan.folders.rend(); ++folder) {
            // 空の場合だけ消す。外部から追加されたfileやdirectoryは保持する。
            std::filesystem::remove(forward ? folder->first : folder->second, error);
            error.clear();
        }
    }
    return success;
}
} // namespace
ContentMutationService::~ContentMutationService() {
    cancellation_.request_stop();
    if (task_.valid()) {
        task_.wait();
    }
}
Result<void> ContentMutationService::Start(std::filesystem::path root,
                                           std::vector<std::pair<std::filesystem::path, std::filesystem::path>> moves) {
    if (task_.valid()) {
        return std::unexpected(Failed("A file operation is running."));
    }
    plan_.reset();
    error_.clear();
    cancellation_ = std::stop_source{};
    task_ = std::async(std::launch::async,
                       [root = std::move(root), moves = std::move(moves), stop = cancellation_.get_token()] {
                           return Prepare(root, moves, stop);
                       });
    return {};
}
void ContentMutationService::Poll() {
    if (!task_.valid() || task_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }
    try {
        auto result = task_.get();
        if (result && !cancellation_.stop_requested()) {
            plan_ = std::move(*result);
        } else if (!result) {
            error_ = result.error().GetMessageText();
        }
    } catch (const std::exception &error) {
        error_ = error.what();
    }
}
void ContentMutationService::Cancel() {
    cancellation_.request_stop();
    plan_.reset();
    error_.clear();
}
bool ContentMutationService::Busy() const {
    return task_.valid();
}
const std::optional<ContentMutationPlan> &ContentMutationService::Plan() const {
    return plan_;
}
const std::string &ContentMutationService::ErrorText() const {
    return error_;
}
Result<ContentMutationPlan>
ContentMutationService::Prepare(const std::filesystem::path &root,
                                std::span<const std::pair<std::filesystem::path, std::filesystem::path>> moves,
                                std::stop_token stop) {
    if (moves.empty() || moves.size() > 256) {
        return std::unexpected(Failed("Select 1 to 256 paths."));
    }
    ContentMutationPlan plan;
    plan.root = root;
    std::map<std::filesystem::path, std::filesystem::path> mapping;
    std::error_code error;
    const ContentItemRegistry registry;
    const auto entries = ContentIndex::Build(root, registry, false, stop);
    if (!entries) {
        return std::unexpected(entries.error());
    }
    for (const auto &[from, to] : moves) {
        const auto checked = ResolveFileWithinRoot(root, AssetCatalog::Utf8(to.lexically_relative(root)));
        const auto source = ResolveFileWithinRoot(root, AssetCatalog::Utf8(from.lexically_relative(root)));
        if (!checked || !source || *source == root || *checked == root || *checked == *source ||
            std::filesystem::exists(*checked, error) || error) {
            return std::unexpected(Failed("Choose an unused destination inside the same root."));
        }
        const bool directory = std::filesystem::is_directory(*source, error);
        if (directory && Inside(*checked, *source)) {
            return std::unexpected(Failed("A folder cannot move inside itself."));
        }
        if (!directory && source->extension() != checked->extension()) {
            return std::unexpected(Failed("Preserve the file extension."));
        }
        if (directory) {
            plan.folders.emplace_back(*source, *checked);
        }
        for (const auto &entry : *entries) {
            if (entry.path == *source || (directory && Inside(entry.path, *source))) {
                const auto destination =
                    entry.path == *source ? *checked : *checked / entry.path.lexically_relative(*source);
                if (entry.directory) {
                    if (entry.path != *source) {
                        plan.folders.emplace_back(entry.path, destination);
                    }
                } else if (!mapping.emplace(entry.path, destination).second) {
                    return std::unexpected(Failed("Overlapping folder selection."));
                }
            }
        }
    }
    if (mapping.size() > 4096) {
        return std::unexpected(Failed("Move budget exceeded (4096 files). Split the operation."));
    }
    if (mapping.empty() && plan.folders.empty()) {
        return std::unexpected(Failed("No movable files in the selection."));
    }
    std::set<std::filesystem::path> destinations;
    for (const auto &[from, to] : mapping) {
        if (!destinations.insert(to).second || std::filesystem::exists(to, error)) {
            return std::unexpected(Failed("Destination collision."));
        }
    }
    uintmax_t budget = 0;
    for (const auto &entry : *entries) {
        if (entry.directory || stop.stop_requested()) {
            continue;
        }
        const auto moved = mapping.find(entry.path);
        const auto target = moved == mapping.end() ? entry.path : moved->second;
        const auto metadata = ContentMetadataProvider::Read(entry.path, root, false, stop);
        std::map<std::string, std::string> replacements;
        if (metadata) {
            for (const auto &dependency : metadata->dependencies) {
                if (dependency.missing || IsAssetIdentity(dependency.reference)) {
                    continue;
                }
                const auto relocated = mapping.find(dependency.resolved);
                const auto destination = relocated == mapping.end() ? dependency.resolved : relocated->second;
                if (destination == dependency.resolved && target == entry.path) {
                    continue;
                }
                const auto rootRelative = Relative(dependency.resolved, root);
                const auto &itemType = registry.Classify(entry.path, false);
                const bool parentRelative = itemType.id == "Model" || itemType.id == "Shader" || itemType.source ||
                                            Extension(entry.path) == ".mtl";
                replacements[dependency.reference] = !parentRelative && dependency.reference == rootRelative
                                                         ? Relative(destination, root)
                                                         : Relative(destination, target.parent_path());
            }
        }
        // includeとNative登録のembedded pathも、元folder基準の明示literalだけを変更する。
        if (registry.Classify(entry.path, false).source || entry.path.filename() == "NexusGeneratedBehaviours.json") {
            for (const auto &[from, to] : mapping) {
                replacements[Relative(from, entry.path.parent_path())] = Relative(to, target.parent_path());
            }
        }
        ContentMutationFile file{entry.path, target, {}, {}, false, entry.bytes, entry.modified};
        if (!replacements.empty()) {
            const auto before = ContentMetadataProvider::Text(entry.path, 128 * 1024 * 1024);
            if (!before) {
                return std::unexpected(before.error());
            }
            auto after = Rewrite(entry.path, *before, replacements);
            if (after && entry.path.filename() == "NexusGeneratedBehaviours.json") {
                try {
                    auto json = nlohmann::json::parse(*after);
                    for (const auto &name : json.at("classes")) {
                        const auto className = name.get<std::string>();
                        const auto header =
                            entry.path.parent_path() /
                            std::filesystem::path(reinterpret_cast<const char8_t *>((className + ".h").c_str()));
                        if (const auto relocated = mapping.find(header); relocated != mapping.end()) {
                            json["locations"][className] = Relative(relocated->second, target.parent_path());
                        }
                    }
                    after = json.dump();
                } catch (const std::exception &failure) {
                    return std::unexpected(Failed(failure.what()));
                }
            }
            if (!after) {
                return std::unexpected(after.error());
            }
            if (*after != *before) {
                budget += before->size() + after->size();
                if (budget > 128 * 1024 * 1024) {
                    return std::unexpected(Failed("Reference rewrite budget exceeded. Split the move."));
                }
                file.before = *before;
                file.after = std::move(*after);
                file.rewritten = true;
            }
        }
        if (moved != mapping.end() || file.rewritten) {
            plan.files.push_back(std::move(file));
        }
    }
    if (stop.stop_requested()) {
        return std::unexpected(Failed("Move preparation cancelled."));
    }
    return plan;
}
Result<void> ContentMutationService::Publish(const AssetDocumentRegistry &documents, EditorHistory &history,
                                             std::shared_ptr<size_t> revision) {
    if (!plan_ || !revision) {
        return std::unexpected(Failed("Prepare the file operation first."));
    }
    for (const auto &file : plan_->files) {
        if (documents.Dirty(file.beforePath) || documents.Dirty(file.afterPath)) {
            return std::unexpected(
                Failed("Save the open document before moving: " + AssetCatalog::Utf8(file.beforePath)));
        }
    }
    if (!Apply(*plan_, true)) {
        return std::unexpected(Failed("Move conflicted with an external edit or I/O failed."));
    }
    auto plan = std::make_shared<ContentMutationPlan>(std::move(*plan_));
    history.Record({"cb.renameMove",
                    [plan, revision] {
                        if (!Apply(*plan, false)) {
                            return false;
                        }
                        ++*revision;
                        return true;
                    },
                    [plan, revision] {
                        if (!Apply(*plan, true)) {
                            return false;
                        }
                        ++*revision;
                        return true;
                    }});
    ++*revision;
    plan_.reset();
    return {};
}
} // namespace NexusEngine
