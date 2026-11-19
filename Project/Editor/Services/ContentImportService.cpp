#include "ContentImportService.h"
#include "ContentMetadata.h"
#include "ContentIndex.h"
#include "Platform/FileSystem/AtomicFile.h"
#include "Platform/FileSystem/AssetIdentity.h"
#include "ThirdParty/nlohmann/json.hpp"
#include <set>
#include <algorithm>
#include <cctype>

namespace NexusEngine {
namespace {
Error Failed(std::string message) {
    return Error(ErrorCategory::Resource, 299, std::move(message));
}
// Undoも外部編集を検証する。UUIDを持つ新規fileは削除せず、所有archiveへ移す。
bool Apply(const ContentImportPlan &plan, bool forward, const std::filesystem::path &archive) {
    std::error_code error;
    for (const auto &file : plan.files) {
        const bool exists = std::filesystem::exists(file.target, error);
        if (error) {
            return false;
        }
        if (forward && !file.existed) {
            const auto held = archive / file.target.lexically_relative(plan.root);
            if (std::filesystem::exists(held, error)) {
                const auto hash = Sha256File(held);
                if (!hash || *hash != file.sourceHash) {
                    return false;
                }
            }
        }
        const bool expected = forward ? file.existed : true;
        if (exists != expected) {
            return false;
        }
        if (exists) {
            const auto hash = Sha256File(file.target);
            if (!hash || *hash != (forward ? file.beforeHash : file.sourceHash)) {
                return false;
            }
        }
    }
    size_t applied = 0;
    const auto write = [&](const ContentImportFile &file, bool next) -> bool {
        const auto held = archive / file.target.lexically_relative(plan.root);
        std::filesystem::create_directories((next ? file.target : held).parent_path(), error);
        if (error) {
            return false;
        }
        if (!file.existed) {
            if (!next) {
                return RenameAssetWithIdentity(plan.root, file.target, held).has_value();
            }
            if (std::filesystem::exists(held, error)) {
                return RenameAssetWithIdentity(plan.root, held, file.target).has_value();
            }
        }
        return WriteFileAtomically(file.target, next ? file.bytes : file.before, file.existed || !next).has_value();
    };
    for (const auto &file : plan.files) {
        if (!write(file, forward)) {
            while (applied > 0) {
                if (!write(plan.files[--applied], !forward)) {
                    return false;
                }
            }
            return false;
        }
        ++applied;
    }
    return true;
}
} // namespace
ContentImportService::~ContentImportService() {
    cancellation_.request_stop();
    if (task_.valid()) {
        task_.wait();
    }
}
Result<void> ContentImportService::Start(std::vector<std::filesystem::path> sources, std::filesystem::path root,
                                         std::filesystem::path destination, ContentImportOptions options) {
    if (task_.valid()) {
        return std::unexpected(Failed("An import is already running."));
    }
    plan_.reset();
    error_.clear();
    cancellation_ = std::stop_source{};
    task_ = std::async(std::launch::async,
                       [sources = std::move(sources), root = std::move(root), destination = std::move(destination),
                        options, stop = cancellation_.get_token()] {
                           return Prepare(sources, root, destination, options, stop);
                       });
    return {};
}
Result<void> ContentImportService::DuplicateFolders(std::filesystem::path root,
                                                    std::vector<std::filesystem::path> folders) {
    if (task_.valid()) {
        return std::unexpected(Failed("An import is already running."));
    }
    plan_.reset();
    error_.clear();
    cancellation_ = std::stop_source{};
    task_ = std::async(
        std::launch::async,
        [root = std::move(root), folders = std::move(folders),
         stop = cancellation_.get_token()]() -> Result<ContentImportPlan> {
            ContentImportOptions options;
            options.dependencies = false;
            std::vector<std::filesystem::path> sources;
            const ContentItemRegistry registry;
            for (const auto &folder : folders) {
                const auto checked = ResolveFileWithinRoot(root, AssetCatalog::Utf8(folder.lexically_relative(root)));
                std::error_code error;
                if (!checked || !std::filesystem::is_directory(*checked, error)) {
                    return std::unexpected(Failed("Select asset folders in this root."));
                }
                std::filesystem::path target;
                for (size_t index = 1; index <= 10000; ++index) {
                    target = folder.parent_path() / (folder.filename().wstring() + L"_Copy" + std::to_wstring(index));
                    if (!std::filesystem::exists(target, error) && !error) {
                        break;
                    }
                }
                const auto entries = ContentIndex::Build(*checked, registry, false, stop);
                if (!entries) {
                    return std::unexpected(entries.error());
                }
                for (const auto &entry : *entries) {
                    if (!entry.directory) {
                        sources.push_back(entry.path);
                        options.relativePaths.push_back(
                            (target / entry.path.lexically_relative(*checked)).lexically_relative(root));
                    }
                }
            }
            return Prepare(sources, root, root, options, stop);
        });
    return {};
}
void ContentImportService::Poll() {
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
    } catch (const std::exception &failure) {
        error_ = failure.what();
    }
}
void ContentImportService::Cancel() {
    cancellation_.request_stop();
    plan_.reset();
    error_.clear();
}
bool ContentImportService::Busy() const {
    return task_.valid();
}
const std::optional<ContentImportPlan> &ContentImportService::Plan() const {
    return plan_;
}
const std::string &ContentImportService::ErrorText() const {
    return error_;
}
Result<ContentImportPlan> ContentImportService::Prepare(std::span<const std::filesystem::path> sources,
                                                        const std::filesystem::path &root,
                                                        const std::filesystem::path &destination,
                                                        ContentImportOptions options, std::stop_token stop) {
    if (sources.empty() || sources.size() > 256) {
        return std::unexpected(Failed("Select 1 to 256 files."));
    }
    ContentImportPlan plan{root, {}, options};
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> queue;
    for (size_t index = 0; index < sources.size(); ++index) {
        const auto &file = sources[index];
        const auto relative =
            options.relativePaths.size() == sources.size()
                ? options.relativePaths[index]
                : (sources.size() == 1 && !options.primaryName.empty() ? options.primaryName : file.filename());
        queue.emplace_back(file, destination / relative);
    }
    std::set<std::filesystem::path> targets;
    const ContentItemRegistry types;
    uintmax_t total = 0;
    for (size_t index = 0; index < queue.size(); ++index) {
        if (stop.stop_requested()) {
            return std::unexpected(Failed("Import cancelled."));
        }
        const auto [source, target] = queue[index];
        const auto checked = ResolveFileWithinRoot(root, AssetCatalog::Utf8(target.lexically_relative(root)));
        std::error_code error;
        if (!checked || !std::filesystem::is_regular_file(source, error) || error || source == *checked) {
            return std::unexpected(Failed("Import source or destination is invalid."));
        }
        if (!targets.insert(*checked).second) {
            const auto prior = std::ranges::find(plan.files, *checked, &ContentImportFile::target);
            if (prior != plan.files.end() && std::filesystem::equivalent(prior->source, source, error)) {
                continue;
            }
            return std::unexpected(Failed("Two import sources use the same destination."));
        }
        const auto &type = types.Classify(source, false);
        auto extension = AssetCatalog::Utf8(source.extension()), filename = AssetCatalog::Utf8(source.filename());
        const auto lower = [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        };
        std::ranges::transform(extension, extension.begin(), lower);
        std::ranges::transform(filename, filename.begin(), lower);
        if (type.source || extension == ".nxmeta" || extension == ".nximport" || filename == ".nexusassets.json") {
            return std::unexpected(Failed("Use the source wizard for code; identity sidecars cannot be imported."));
        }
        auto targetExtension = AssetCatalog::Utf8(checked->extension()),
             targetName = AssetCatalog::Utf8(checked->filename());
        std::ranges::transform(targetExtension, targetExtension.begin(), lower);
        std::ranges::transform(targetName, targetName.begin(), lower);
        if (targetExtension != extension || targetExtension == ".nxmeta" || targetExtension == ".nximport" ||
            targetName == ".nexusassets.json") {
            return std::unexpected(Failed("Preserve the source format and do not replace identity metadata."));
        }
        const auto size = std::filesystem::file_size(source, error);
        total += size;
        if (error || total > 128 * 1024 * 1024 || queue.size() > 1024) {
            return std::unexpected(Failed("Import review budget exceeded (128 MiB / 1024 files). Split the import."));
        }
        auto bytes = ContentMetadataProvider::Text(source, 128 * 1024 * 1024);
        if (!bytes) {
            return std::unexpected(bytes.error());
        }
        const auto hash = Sha256Bytes(*bytes);
        const auto currentSource = Sha256File(source);
        if (!currentSource || !hash || *currentSource != *hash) {
            return std::unexpected(
                Failed("The import source changed during preparation. Retry after it finishes writing."));
        }
        if (!hash) {
            return std::unexpected(hash.error());
        }
        ContentImportFile file{source, *checked, std::move(*bytes), {}, {}, *hash, false};
        file.existed = std::filesystem::exists(*checked, error);
        if (error || (file.existed && !options.replace)) {
            return std::unexpected(
                Failed("Destination exists. Enable replacement explicitly or choose another folder."));
        }
        if (file.existed) {
            const auto before = ContentMetadataProvider::Text(*checked, 128 * 1024 * 1024);
            const auto beforeHash =
                before ? Sha256Bytes(*before) : Result<std::string>{std::unexpected(Failed("Cannot read backup."))};
            if (!before || !beforeHash) {
                return std::unexpected(Failed("Cannot back up the destination."));
            }
            total += before->size();
            if (total > 128 * 1024 * 1024) {
                return std::unexpected(Failed("Import backup budget exceeded."));
            }
            file.before = *before;
            file.beforeHash = *beforeHash;
        }
        plan.files.push_back(std::move(file));
        if (options.dependencies) {
            // 同じ外部folder内の相対依存は、同じ配置を保って一緒にコピーする。
            const auto metadata = ContentMetadataProvider::Read(source, source.parent_path(), false, stop);
            if (metadata) {
                for (const auto &dependency : metadata->dependencies) {
                    if (dependency.missing) {
                        return std::unexpected(Failed("Unresolved import dependency: " + dependency.reference));
                    }
                    queue.emplace_back(dependency.resolved,
                                       target.parent_path() /
                                           dependency.resolved.lexically_relative(source.parent_path()));
                }
            }
        }
    }
    return plan;
}
Result<void> ContentImportService::Publish(AssetCatalog &catalog, const AssetDocumentRegistry &documents,
                                           EditorHistory &history, std::shared_ptr<size_t> revision) {
    if (!plan_ || !revision || plan_->root != catalog.Root()) {
        return std::unexpected(Failed("No import plan for this root."));
    }
    for (const auto &file : plan_->files) {
        if (documents.Dirty(file.target)) {
            return std::unexpected(
                Failed("Save the open document before importing: " + AssetCatalog::Utf8(file.target)));
        }
    }
    const auto uuid = CreateUuid();
    if (!uuid) {
        return std::unexpected(uuid.error());
    }
    const auto archive = catalog.Root() / ".nexus-trash" / *uuid;
    if (!Apply(*plan_, true, archive)) {
        return std::unexpected(Failed("Import conflicted with an external change or could not be written."));
    }
    for (const auto &file : plan_->files) {
        const auto identity = EnsureAssetIdentity(catalog.Root(), file.target);
        if (!identity) {
            static_cast<void>(Apply(*plan_, false, archive));
            return std::unexpected(identity.error());
        }
    }
    auto plan = std::make_shared<const ContentImportPlan>(std::move(*plan_));
    history.Record({"cb.import",
                    [plan, archive, revision] {
                        if (!Apply(*plan, false, archive)) {
                            return false;
                        }
                        ++*revision;
                        return true;
                    },
                    [plan, archive, revision] {
                        if (!Apply(*plan, true, archive)) {
                            return false;
                        }
                        ++*revision;
                        return true;
                    }});
    ++*revision;
    catalog.Refresh();
    plan_.reset();
    return {};
}
} // namespace NexusEngine
