#include "ContentBrowserServices.h"
#include "Platform/FileSystem/AtomicFile.h"
#include "Platform/FileSystem/AssetIdentity.h"
#include "Runtime/Reflection/TypeRegistry.h"
#include "Foundation/Utility/String/ConvertString.h"
#include "ThirdParty/nlohmann/json.hpp"

namespace NexusEngine {
std::optional<ContentImportOrigin> ContentBrowserServices::Origin(const ContentReference &reference) const {
    if (!settings) {
        return {};
    }
    const auto key = "import:" + reference.root + ":" + reference.reference;
    const auto saved = settings->user.contentBrowser.find(key);
    if (saved == settings->user.contentBrowser.end()) {
        return {};
    }
    auto &cached = origins_[key];
    if (cached.first != saved->second) {
        cached.first = saved->second;
        cached.second.reset();
        try {
            const auto json = nlohmann::json::parse(saved->second);
            cached.second =
                ContentImportOrigin{std::filesystem::path(ConvertString(json.at("source").get<std::string>())),
                                    json.value("hash", std::string{}), json.value("dependencies", true)};
        } catch (const std::exception &) {
        }
    }
    return cached.second;
}
Result<void>
ContentBrowserServices::RememberOrigins(std::span<const std::pair<ContentReference, ContentImportOrigin>> origins) {
    if (!settings) {
        return {};
    }
    for (const auto &[reference, origin] : origins) {
        settings->user.contentBrowser["import:" + reference.root + ":" + reference.reference] = nlohmann::json{
            {"source", AssetCatalog::Utf8(origin.source)},
            {"hash", origin.hash},
            {"dependencies", origin.dependencies}}.dump();
    }
    if (!settings->SaveUser()) {
        return std::unexpected(
            Error(ErrorCategory::Resource, 299, "Import completed, but local origins could not be saved."));
    }
    return {};
}
void ContentBrowserServices::AddRoot(std::string id, std::string label, std::string icon, AssetCatalog &catalog,
                                     bool readOnly, bool source) {
    roots.push_back({std::move(id),
                     std::move(label),
                     std::move(icon),
                     &catalog,
                     readOnly,
                     source,
                     false,
                     false,
                     {},
                     std::make_shared<ContentIndex>(),
                     catalog.Revision()});
}
void ContentBrowserServices::AddOwnedRoot(std::string id, std::string label, std::string icon,
                                          std::filesystem::path path, bool readOnly, bool source, bool engine,
                                          bool plugin) {
    auto catalog = std::make_shared<AssetCatalog>(std::move(path));
    AddRoot(std::move(id), std::move(label), std::move(icon), *catalog, readOnly, source);
    roots.back().owner = std::move(catalog);
    roots.back().engine = engine;
    roots.back().plugin = plugin;
}
std::optional<size_t> ContentBrowserServices::RootFor(const std::filesystem::path &file) const {
    std::optional<size_t> result;
    size_t length = 0;
    for (size_t index = 0; index < roots.size(); ++index) {
        const auto &root = roots[index].catalog->Root();
        const auto relative = file.lexically_relative(root);
        if (!relative.empty() && !relative.is_absolute() && *relative.begin() != ".." &&
            (root.native().size() > length ||
             (root.native().size() == length && (!result || (roots[*result].readOnly && !roots[index].readOnly))))) {
            result = index;
            length = root.native().size();
        }
    }
    return result;
}
Result<ContentReference> ContentBrowserServices::Reference(const std::filesystem::path &file) {
    const auto root = RootFor(file);
    if (!root) {
        return std::unexpected(Error(ErrorCategory::Resource, 297, "The file is outside registered Content roots."));
    }
    const auto &value = roots[*root];
    std::error_code error;
    if (std::filesystem::is_regular_file(file, error) && !error && !value.readOnly) {
        const auto reference = value.catalog->StableReference(file);
        if (!reference) {
            return std::unexpected(reference.error());
        }
        return ContentReference{value.id, *reference};
    }
    return ContentReference{value.id, AssetCatalog::Utf8(file.lexically_relative(value.catalog->Root()))};
}
Result<std::filesystem::path> ContentBrowserServices::Resolve(const ContentReference &reference) const {
    for (const auto &root : roots) {
        if (root.id == reference.root) {
            if (reference.reference == ".") {
                return root.catalog->Root();
            }
            return ResolveAssetReference(root.catalog->Root(), reference.reference);
        }
    }
    return std::unexpected(Error(ErrorCategory::Resource, 297, "The referenced Content root is unavailable."));
}
std::vector<ContentGraphRoot> ContentBrowserServices::GraphRoots() const {
    std::vector<ContentGraphRoot> result;
    for (const auto &root : roots) {
        result.push_back({root.id, root.catalog->Root(), root.engine || root.plugin});
    }
    return result;
}
void ContentBrowserServices::UpdateSourceTypes(const TypeRegistry &types, const std::filesystem::path &sourceRoot) {
    if (typeGeneration_ == types.Generation()) {
        return;
    }
    typeGeneration_ = types.Generation();
    std::map<std::string, std::vector<std::filesystem::path>> bindings;
    for (const auto &[id, type] : types.Types()) {
        for (const auto &source : type.sourceFiles) {
            const auto file = std::filesystem::path(ConvertString(source));
            // 登録変更のUI処理では値だけを正規化する。実在性とaliasはGraph workerで調べる。
            const auto candidate = (file.is_absolute() ? file : sourceRoot / file).lexically_normal();
            const auto relative = candidate.lexically_relative(sourceRoot);
            const std::optional<std::filesystem::path> checked =
                (!relative.empty() && !relative.is_absolute() && *relative.begin() != "..")
                    ? std::optional<std::filesystem::path>{candidate}
                    : std::nullopt;
            if (checked) {
                bindings[id].push_back(*checked);
                if (id.starts_with("ManagedScript:")) {
                    bindings[id.substr(14)].push_back(*checked);
                }
            }
        }
    }
    dependencies.SetSourceTypes(std::move(bindings));
}
void ContentBrowserServices::Poll() {
    if (observedFileRevision_ != *fileRevision) {
        observedFileRevision_ = *fileRevision;
        // 操作元のBrowserを閉じても、手動監視モードのUndo/Redoを全windowへ通知する。
        for (auto &root : roots) {
            root.catalog->Refresh();
        }
    }

    for (auto &root : roots) {
        root.catalog->PollChanges();
        root.index->Poll();
        if (root.observedRevision != root.catalog->Revision()) {
            root.observedRevision = root.catalog->Revision();
            if (root.id == "Content" && contentChanged) {
                contentChanged();
            }
            root.index->Invalidate();
            dependencies.Invalidate();
            if (!selection_.empty()) {
                const auto selectedRoot = RootFor(selection_.back());
                if (selectedRoot && roots[*selectedRoot].catalog == root.catalog) {
                    metadata.Invalidate();
                }
            }
        }
    }
    dependencies.Poll();
    metadata.Poll();
}
void ContentBrowserServices::Select(std::vector<std::filesystem::path> files, size_t sceneRevision,
                                    ContentInspectorMode mode) {
    selection_ = std::move(files);
    sceneRevision_ = sceneRevision;
    mode_ = mode;
    if (showInspector && !selection_.empty()) {
        showInspector();
    }
    if (!selection_.empty()) {
        const auto root = RootFor(selection_.back());
        if (root) {
            static_cast<void>(Reference(selection_.back()));
            metadata.Request(selection_.back(), roots[*root].catalog->Root());
        }
    }
    if (mode != ContentInspectorMode::Metadata) {
        dependencies.Request(GraphRoots());
    }
}
void ContentBrowserServices::ClearSelection() {
    selection_.clear();
}
bool ContentBrowserServices::SelectionActive(size_t sceneRevision) const {
    return !selection_.empty() && sceneRevision_ == sceneRevision;
}
std::span<const std::filesystem::path> ContentBrowserServices::Selected() const {
    return selection_;
}
ContentInspectorMode ContentBrowserServices::InspectorMode() const {
    return mode_;
}
} // namespace NexusEngine
