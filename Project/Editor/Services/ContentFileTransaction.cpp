#include "ContentFileTransaction.h"
#include "Platform/FileSystem/AssetIdentity.h"
#include "Platform/FileSystem/AtomicFile.h"
#include <algorithm>
#include <set>

namespace NexusEngine {
namespace {
using Pair = std::pair<std::filesystem::path, std::filesystem::path>;
Error Failed(std::string text) {
    return Error(ErrorCategory::FileSystem, 283, std::move(text));
}
bool MoveBatch(const std::filesystem::path &root, const std::vector<Pair> &pairs, bool reverse) {
    // redo / undoでも外部変更を検証し、既存宛先を置換しない。
    size_t applied = 0;
    for (const auto &pair : pairs) {
        const auto &from = reverse ? pair.second : pair.first;
        const auto &to = reverse ? pair.first : pair.second;
        if (!RenameAssetWithIdentity(root, from, to)) {
            while (applied > 0) {
                const auto &rollback = pairs[--applied];
                const auto restored = RenameAssetWithIdentity(root, reverse ? rollback.first : rollback.second,
                                                              reverse ? rollback.second : rollback.first);
                if (!restored) {
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
Result<void> ContentFileTransaction::Rename(AssetCatalog &catalog, const std::filesystem::path &source,
                                            std::string_view relative, EditorHistory &history,
                                            std::shared_ptr<size_t> revision) {
    const auto target = ResolveFileWithinRoot(catalog.Root(), std::string(relative));
    if (!target) {
        return std::unexpected(target.error());
    }
    if (!revision || catalog.Types().Classify(source, false).source) {
        return std::unexpected(Failed("Source changes require module migration."));
    }
    if (source.parent_path() != target->parent_path() && !catalog.Types().Classify(source, false).moveAcrossFolders) {
        return std::unexpected(Failed("This format may contain relative dependencies. Rename within the same folder, "
                                      "or use a dependency-aware importer."));
    }
    const auto root = catalog.Root();
    const std::vector<Pair> pairs{{source, *target}};
    if (!MoveBatch(root, pairs, false)) {
        return std::unexpected(Failed("The Asset could not be renamed."));
    }
    history.Record({"cb.renameMove",
                    [root, pairs, revision] {
                        if (!MoveBatch(root, pairs, true)) {
                            return false;
                        }
                        ++*revision;
                        return true;
                    },
                    [root, pairs, revision] {
                        if (!MoveBatch(root, pairs, false)) {
                            return false;
                        }
                        ++*revision;
                        return true;
                    }});
    ++*revision;
    catalog.Refresh();
    return {};
}
Result<void> ContentFileTransaction::Execute(AssetCatalog &catalog, std::span<const std::filesystem::path> files,
                                             ContentFileOperation operation, const std::filesystem::path &destination,
                                             EditorHistory &history, std::shared_ptr<size_t> revision) {
    if (files.empty() || files.size() > 256 || !revision) {
        return std::unexpected(Failed("Select 1 to 256 Asset files."));
    }
    const auto root = catalog.Root();
    auto uuid = CreateUuid();
    if (!uuid) {
        return std::unexpected(uuid.error());
    }
    const auto checkedArchive = ResolveFileWithinRoot(root, ".nexus-trash/" + *uuid);
    if (!checkedArchive) {
        return std::unexpected(checkedArchive.error());
    }
    const auto archive = *checkedArchive;
    std::error_code error;
    std::vector<Pair> pairs;
    std::set<std::filesystem::path> unique;
    for (const auto &path : files) {
        const auto source = ResolveFileWithinRoot(root, AssetCatalog::Utf8(path.lexically_relative(root)));
        if (!source || !std::filesystem::is_regular_file(*source, error) || error || !unique.insert(*source).second ||
            catalog.Types().Classify(*source, false).source || source->extension() == ".nxmeta" ||
            source->filename() == ".nexusassets.json" ||
            AssetCatalog::Utf8(source->lexically_relative(root)).starts_with(".nexus-trash")) {
            return std::unexpected(Failed("Only unique Asset files inside this root can be changed. Source and "
                                          "directory operations require their own migration service."));
        }
        if (operation != ContentFileOperation::Duplicate &&
            !catalog.Types().Classify(*source, false).moveAcrossFolders) {
            return std::unexpected(Failed("Moving or archiving this format requires dependency review. Its relative "
                                          "external files are not rewritten by UUID renaming."));
        }
        auto target =
            operation == ContentFileOperation::Archive ? archive / path.filename() : destination / path.filename();
        if (operation == ContentFileOperation::Duplicate) {
            const auto stem = path.stem().wstring();
            size_t suffix = 1;
            do {
                target =
                    path.parent_path() / (stem + L"_Copy" + std::to_wstring(suffix++) + path.extension().wstring());
            } while (std::filesystem::exists(target, error) && !error && suffix <= 10000);
        }
        const auto checked = ResolveFileWithinRoot(root, AssetCatalog::Utf8(target.lexically_relative(root)));
        if (!checked || error || std::filesystem::exists(*checked, error) || error ||
            std::filesystem::exists(std::filesystem::path(checked->wstring() + L".nxmeta"), error)) {
            return std::unexpected(Failed("The destination must be unused and inside the Asset root."));
        }
        if (std::ranges::any_of(pairs, [&](const auto &item) {
                return item.second == *checked;
            })) {
            return std::unexpected(Failed("Duplicate destination filenames."));
        }
        pairs.emplace_back(*source, *checked);
    }
    if (operation == ContentFileOperation::Archive || operation == ContentFileOperation::Duplicate) {
        std::filesystem::create_directories(archive, error);
        if (error) {
            return std::unexpected(Failed(error.message()));
        }
    }
    if (operation == ContentFileOperation::Duplicate) {
        std::vector<std::filesystem::path> created;
        for (const auto &[from, to] : pairs) {
            const bool copied = std::filesystem::copy_file(from, to, std::filesystem::copy_options::none, error);
            if (copied) {
                created.push_back(to);
            }
            if (!copied || error || !EnsureAssetIdentity(root, to)) {
                // 既存fileは置換せず、失敗時には自分が作ったcopyだけを所有archiveへ退避する。
                // I/O障害でUUIDの登録に失敗していても、元Assetを削除・上書きしない。
                for (size_t i = 0; i < created.size(); ++i) {
                    const auto owned = archive / (std::to_wstring(i) + L"_" + created[i].filename().wstring());
                    if (!RenameAssetWithIdentity(root, created[i], owned)) {
                        std::error_code rollback;
                        std::filesystem::rename(created[i], owned, rollback);
                        const auto sidecar = std::filesystem::path(created[i].wstring() + L".nxmeta");
                        if (!rollback && std::filesystem::exists(sidecar, rollback)) {
                            std::filesystem::rename(sidecar, std::filesystem::path(owned.wstring() + L".nxmeta"),
                                                    rollback);
                        }
                    }
                }
                catalog.Refresh();
                return std::unexpected(Failed("The copy transaction failed. Completed copies were archived where "
                                              "possible; original Assets were preserved."));
            }
        }
        // Duplicateのundoはcopyだけを退避する。元fileを削除せず、redoでも同じUUIDを復元する。
        std::vector<Pair> restore;
        for (size_t i = 0; i < pairs.size(); ++i) {
            restore.emplace_back(archive / (std::to_wstring(i) + L"_" + pairs[i].second.filename().wstring()),
                                 pairs[i].second);
        }
        pairs = std::move(restore);
    } else if (!MoveBatch(root, pairs, false)) {
        return std::unexpected(Failed("The batch move was rejected. Check destination paths and permissions."));
    }
    history.Record({"cb.fileTransaction",
                    [root, pairs, revision] {
                        if (!MoveBatch(root, pairs, true)) {
                            return false;
                        }
                        ++*revision;
                        return true;
                    },
                    [root, pairs, revision] {
                        if (!MoveBatch(root, pairs, false)) {
                            return false;
                        }
                        ++*revision;
                        return true;
                    }});
    ++*revision;
    catalog.Refresh();
    return {};
}
} // namespace NexusEngine
