#include "ContentBrowserPanel.h"
#include "Editor/Gui/Icons/EditorIcons.h"
#include "Editor/Core/EditorSession.h"
#include "Editor/Services/ContentDragPayload.h"
#include "Editor/Services/LocalizationService.h"
#include "Platform/FileSystem/AtomicFile.h"
#include <algorithm>
#include <set>
#include <cstdio>
#include <sstream>

namespace NexusEngine {
void ContentBrowserPanel::SyncRoots() {
    while (roots_.size() < services_->roots.size()) {
        const auto &root = services_->roots[roots_.size()];
        roots_.push_back({root.label, root.icon, root.catalog, root.catalog->Root(), root.id, root.readOnly,
                          root.source, root.engine, root.plugin});
    }
}
void ContentBrowserPanel::ChooseFolder(size_t root, const std::filesystem::path &path, bool toggle) {
    if (!toggle) {
        pendingNavigate_ = ContentLocation{root, path};
        return;
    }
    virtualView_.clear();
    collectionId_.clear();
    const ContentLocation location{root, path};
    const auto found = std::ranges::find(folders_, location);
    if (found == folders_.end()) {
        folders_.push_back(location);
    } else if (folders_.size() > 1) {
        folders_.erase(found);
    }
    state_.selection.clear();
}
std::shared_ptr<const std::vector<AssetEntry>> ContentBrowserPanel::Listing() {
    std::vector<std::shared_ptr<const std::vector<AssetEntry>>> inputs;
    std::ostringstream key;
    key << virtualView_ << ':' << collectionId_ << ':' << recursive_;
    if (virtualView_ == "Collection" || virtualView_ == "Recent") {
        key << services_->library.Generation();
        for (const auto &root : services_->roots) {
            key << ':' << root.catalog->Revision();
        }
        if (merged_ && listingKey_ == key.str()) {
            return merged_;
        }
        std::vector<ContentReference> references;
        if (virtualView_ == "Collection") {
            if (const auto *collection = services_->library.Find(collectionId_)) {
                references = collection->items;
            }
        } else {
            const auto recent = services_->library.Recent();
            references.assign(recent.begin(), recent.end());
        }
        std::vector<AssetEntry> entries;
        for (const auto &reference : references) {
            const auto path = services_->Resolve(reference);
            if (!path) {
                AssetEntry missing;
                missing.path = std::filesystem::path(reference.reference);
                missing.name = reference.reference;
                missing.typeKey = "asset.file";
                missing.missing = true;
                entries.push_back(std::move(missing));
                continue;
            }
            const auto root = services_->RootFor(*path);
            if (!root) {
                continue;
            }
            std::error_code error;
            const bool folder = std::filesystem::is_directory(*path, error);
            error.clear();
            const auto &type = services_->roots[*root].catalog->Types().Classify(*path, folder);
            AssetEntry entry{*path, AssetCatalog::Utf8(path->filename()), type.labelKey, folder};
            entry.missing = !std::filesystem::exists(*path, error) || error;
            error.clear();
            if (!folder && !entry.missing) {
                entry.bytes = std::filesystem::file_size(*path, error);
                error.clear();
            }
            entry.modified = std::filesystem::last_write_time(*path, error);
            error.clear();
            entries.push_back(std::move(entry));
        }
        listingKey_ = key.str();
        merged_ = std::make_shared<const std::vector<AssetEntry>>(std::move(entries));
        return merged_;
    }
    const auto query = ContentSearchParser::Parse(state_.search);
    const bool metrics = std::ranges::any_of(query.terms, [](const auto &term) {
        return term.field == "triangles";
    });
    const bool indexed = recursive_ || metrics || virtualView_ == "Modified";
    // まず共有snapshotと世代だけを比較する。idle frameでは全件のcopy/filterをしない。
    for (const auto &location : folders_) {
        if (location.root >= roots_.size()) {
            continue;
        }
        auto &root = roots_[location.root];
        key << ':' << root.id << ':' << AssetCatalog::Utf8(location.path);
        if (indexed) {
            auto &index = *services_->roots[location.root].index;
            index.Request(root.catalog->Root(), root.catalog->Types(), metrics);
            key << ':' << index.Revision();
            inputs.push_back(index.Snapshot());
        } else {
            inputs.push_back(root.catalog->Snapshot(location.path));
        }
    }
    if (merged_ && listingKey_ == key.str() && inputs == inputs_) {
        return merged_;
    }
    auto originalInputs = inputs;
    if (indexed) {
        size_t i = 0;
        for (const auto &location : folders_) {
            if (location.root >= roots_.size()) {
                continue;
            }
            std::vector<AssetEntry> scoped;
            for (const auto &entry : *inputs[i]) {
                const auto relative = entry.path.lexically_relative(location.path);
                const bool descendant = !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
                if (descendant &&
                    (recursive_ || virtualView_ == "Modified" || entry.path.parent_path() == location.path)) {
                    scoped.push_back(entry);
                }
            }
            inputs[i++] = std::make_shared<const std::vector<AssetEntry>>(std::move(scoped));
        }
    }
    std::vector<AssetEntry> merged;
    std::set<std::filesystem::path> unique;
    for (const auto &input : inputs) {
        for (const auto &entry : *input) {
            if (unique.insert(entry.path).second) {
                merged.push_back(entry);
            }
        }
    }
    inputs_ = std::move(originalInputs);
    listingKey_ = key.str();
    merged_ = std::make_shared<const std::vector<AssetEntry>>(std::move(merged));
    return merged_;
}
void ContentBrowserPanel::DrawLibrary(const EditorContext &context) {
    ImGui::Separator();
    if (ImGui::CollapsingHeader(context.text.Text("cb.collections").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        if (UI::IconMenuItem("add", context.text.Text("cb.newCollection").c_str())) {
            collectionCreate_ = true;
        }
        for (const auto &collection : services_->library.Collections()) {
            ImGui::PushID(collection.id.c_str());
            UI::EditorIcon("collection");
            ImGui::SameLine();
            const auto label = collection.name + (collection.shared ? " [Shared]" : " [Local]");
            if (ImGui::Selectable(label.c_str(), virtualView_ == "Collection" && collectionId_ == collection.id)) {
                virtualView_ = "Collection";
                collectionId_ = collection.id;
                state_.selection.clear();
            }
            if (ImGui::BeginDragDropTarget()) {
                auto *payload = ImGui::AcceptDragDropPayload("NEXUS_CONTENT_ITEMS");
                const bool multi = payload != nullptr;
                if (!payload) {
                    payload = ImGui::AcceptDragDropPayload("NEXUS_ASSET_PATH");
                }
                if (!payload) {
                    payload = ImGui::AcceptDragDropPayload("NEXUS_SOURCE_PATH");
                }
                if (payload) {
                    const auto files =
                        multi ? ContentDragPayload::Decode(payload->Data, static_cast<size_t>(payload->DataSize))
                              : ContentDragPayload::Legacy(payload->Data, static_cast<size_t>(payload->DataSize));
                    std::vector<ContentReference> refs;
                    if (files) {
                        for (const auto &file : *files) {
                            const auto reference = services_->Reference(file);
                            if (reference) {
                                refs.push_back(*reference);
                            }
                        }
                        const auto result = services_->library.Add(collection.id, refs);
                        if (!result) {
                            error_ = result.error().GetMessageText();
                        }
                    } else {
                        error_ = files.error().GetMessageText();
                    }
                }
                ImGui::EndDragDropTarget();
            }
            if (ImGui::BeginPopupContextItem("CollectionActions")) {
                if (UI::IconMenuItem("rename", context.text.Text("cb.renameCollection").c_str())) {
                    collectionRename_ = collection.id;
                    std::snprintf(libraryName_.data(), libraryName_.size(), "%s", collection.name.c_str());
                }
                if (UI::IconMenuItem("delete", context.text.Text("cb.deleteCollection").c_str())) {
                    collectionDelete_ = collection.id;
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
    }
    if (ImGui::Selectable(context.text.Text("cb.recentOpened").c_str(), virtualView_ == "Recent")) {
        virtualView_ = "Recent";
        state_.selection.clear();
    }
    if (ImGui::Selectable(context.text.Text("cb.recentModified").c_str(), virtualView_ == "Modified")) {
        virtualView_ = "Modified";
        folders_.clear();
        for (size_t index = 0; index < roots_.size(); ++index) {
            if (!roots_[index].engine && !roots_[index].plugin) {
                folders_.push_back({index, roots_[index].catalog->Root()});
            }
        }
        state_.folderFirst = false;
        state_.sort = ContentSort::Modified;
        state_.descending = true;
    }
    if (ImGui::CollapsingHeader(context.text.Text("cb.savedSearches").c_str())) {
        if (UI::IconMenuItem("add", context.text.Text("cb.saveSearch").c_str())) {
            searchCreate_ = true;
        }
        for (const auto &search : services_->library.Searches()) {
            ImGui::PushID(search.name.c_str());
            if (ImGui::Selectable(search.name.c_str())) {
                state_.search = search.query;
                recursive_ = search.recursive;
                virtualView_.clear();
                state_.filters = {search.filters.begin(), search.filters.end()};
                if (!search.scopes.empty()) {
                    folders_.clear();
                    for (const auto &scope : search.scopes) {
                        const auto resolved = services_->Resolve(scope);
                        const auto root = resolved ? services_->RootFor(*resolved) : std::optional<size_t>{};
                        if (root) {
                            folders_.push_back({*root, *resolved});
                        }
                    }
                    if (folders_.empty()) {
                        folders_.push_back({rootIndex_, roots_[rootIndex_].current});
                    }
                }
            }
            if (ImGui::BeginPopupContextItem("SavedSearchActions")) {
                if (UI::IconMenuItem("delete", context.text.Text("cb.removeSearch").c_str())) {
                    searchDelete_ = search.name;
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
    }
}
void ContentBrowserPanel::DrawLibraryDialogs(const EditorContext &context) {
    if (!collectionDelete_.empty()) {
        const auto result = services_->library.Erase(collectionDelete_);
        if (!result) {
            error_ = result.error().GetMessageText();
        }
        collectionDelete_.clear();
    }
    if (!searchDelete_.empty()) {
        const auto result = services_->library.RemoveSearch(searchDelete_);
        if (!result) {
            error_ = result.error().GetMessageText();
        }
        searchDelete_.clear();
    }
    if (collectionCreate_ || searchCreate_) {
        libraryName_.fill(0);
        ImGui::OpenPopup(searchCreate_ ? "ContentSavedSearch" : "ContentCollection");
        collectionCreate_ = searchCreate_ = false;
    }
    if (!collectionRename_.empty() && !ImGui::IsPopupOpen("ContentRenameCollection")) {
        ImGui::OpenPopup("ContentRenameCollection");
    }
    for (const auto *popup : {"ContentCollection", "ContentRenameCollection", "ContentSavedSearch"}) {
        if (!ImGui::BeginPopupModal(popup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            continue;
        }
        ImGui::InputText(context.text.Text("cb.name").c_str(), libraryName_.data(), libraryName_.size());
        if (std::string_view(popup) == "ContentCollection") {
            ImGui::Checkbox(context.text.Text("cb.sharedCollection").c_str(), &sharedCollection_);
        }
        if (ImGui::Button(context.text.Text("cb.apply").c_str())) {
            Result<void> result;
            if (std::string_view(popup) == "ContentSavedSearch") {
                ContentSavedSearch search{libraryName_.data(), state_.search, recursive_};
                search.filters.assign(state_.filters.begin(), state_.filters.end());
                for (const auto &scope : folders_) {
                    if (scope.root < roots_.size()) {
                        search.scopes.push_back(
                            {roots_[scope.root].id,
                             AssetCatalog::Utf8(scope.path.lexically_relative(roots_[scope.root].catalog->Root()))});
                    }
                }
                result = services_->library.SaveSearch(std::move(search));
            } else if (std::string_view(popup) == "ContentRenameCollection") {
                result = services_->library.Rename(collectionRename_, libraryName_.data());
            } else {
                const auto created = services_->library.Create(libraryName_.data(), sharedCollection_);
                if (!created) {
                    result = std::unexpected(created.error());
                }
            }
            if (result) {
                collectionRename_.clear();
                ImGui::CloseCurrentPopup();
            } else {
                error_ = result.error().GetMessageText();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(context.text.Text("cancel").c_str())) {
            collectionRename_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
} // namespace NexusEngine
