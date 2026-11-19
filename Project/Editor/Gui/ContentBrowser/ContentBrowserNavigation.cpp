#include "ContentBrowserPanel.h"
#include "Editor/Gui/Icons/EditorIcons.h"
#include "Editor/Gui/Widgets/SearchBar.h"
#include "Editor/Gui/Widgets/AssetPropertyWidget.h"
#include "Editor/Services/LocalizationService.h"
#include "Editor/Services/EditorCommandRegistry.h"
#include "Editor/Core/EditorSession.h"
#include "Foundation/Utility/String/ConvertString.h"
#include <algorithm>
#include "Editor/Services/ContentDragPayload.h"
#include <cstdio>

namespace NexusEngine {
void ContentBrowserPanel::DrawToolbar(const EditorContext &context) {
    auto &catalog = *roots_[rootIndex_].catalog;
    const auto button = [&](const char *icon, const char *key, bool enabled = true, bool selected = false) {
        const auto label = context.text.Label(key, icon);
        return UI::IconButton(icon, label.c_str(), enabled, selected);
    };
    if (button("back", "cb.back", state_.history.CanBack())) {
        if (auto location = state_.history.Back()) {
            Navigate(location->root, location->path, false);
        }
    }
    ImGui::SameLine();
    if (button("forward", "cb.forward", state_.history.CanForward())) {
        if (auto location = state_.history.Forward()) {
            Navigate(location->root, location->path, false);
        }
    }
    ImGui::SameLine();
    if (button("folder_up", "asset.up", roots_[rootIndex_].current != catalog.Root())) {
        Navigate(rootIndex_, roots_[rootIndex_].current.parent_path());
    }
    ImGui::SameLine();
    if (button("add", "browser.create", !context.session.IsPlaying() && !roots_[rootIndex_].readOnly)) {
        ImGui::OpenPopup("ContentAdd");
    }
    if (ImGui::BeginPopup("ContentAdd")) {
        DrawCreateMenu(context, roots_[rootIndex_].current);
        if (UI::IconMenuItem("folder", context.text.Text("cb.newFolder").c_str(), false,
                             !roots_[rootIndex_].readOnly)) {
            folderRequested_ = true;
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (button("import", "cb.import",
               !context.session.IsPlaying() && !roots_[rootIndex_].readOnly && !roots_[rootIndex_].source)) {
        RequestImport();
    }
    ImGui::SameLine();
    if (button("save", "cb.saveLevel", context.commands.CanExecute("file.save"))) {
        context.commands.Execute("file.save");
    }
    ImGui::SameLine();
    if (button("save_all", "cb.saveAll", services_->documents.AnyDirty() && !context.session.IsPlaying())) {
        const auto result = services_->documents.SaveAll();
        if (!result) {
            error_ = result.error().GetMessageText();
        }
    }
    ImGui::SameLine();
    if (button("refresh", "asset.refresh")) {
        for (const auto &root : roots_) {
            root.catalog->Refresh();
        }
        const auto libraryReload = services_->library.Reload();
        if (!libraryReload) {
            error_ = libraryReload.error().GetMessageText();
        }
        if (context.assets) {
            context.assets->RefreshPreviews();
        }
    }
    ImGui::SameLine();
    if (button("copy_path", "cb.editPath")) {
        ImGui::OpenPopup("ContentPathInput");
    }
    if (context.session.IsDirty()) {
        ImGui::SameLine();
        UI::EditorIcon("dirty");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", context.text.Text("cb.dirtyLevel").c_str());
        }
    }
    // Breadcrumbは現在root内の値だけを分割する。描画のたびにfilesystemを問い合わせない。
    ImGui::SameLine();
    const auto &active = roots_[rootIndex_];
    if (ImGui::SmallButton(context.text.Text(active.label).c_str())) {
        pendingNavigate_ = ContentLocation{rootIndex_, active.catalog->Root()};
    }
    auto path = active.catalog->Root();
    for (const auto &part : active.current.lexically_relative(path)) {
        if (part == "." || part.empty()) {
            continue;
        }
        path /= part;
        if (ImGui::GetContentRegionAvail().x > ImGui::CalcTextSize(AssetCatalog::Utf8(part).c_str()).x + 80) {
            ImGui::SameLine();
        }
        ImGui::TextUnformatted(">");
        ImGui::SameLine();
        ImGui::PushID(AssetCatalog::Utf8(path).c_str());
        if (ImGui::SmallButton(AssetCatalog::Utf8(part).c_str())) {
            pendingNavigate_ = ContentLocation{rootIndex_, path};
        }
        ImGui::PopID();
    }
    if (ImGui::BeginPopup("ContentPathInput")) {
        ImGui::SetNextItemWidth(420);
        if (ImGui::InputTextWithHint("##ContentDirectPath", context.text.Text("cb.pathHint").c_str(),
                                     directPath_.data(), directPath_.size(), ImGuiInputTextFlags_EnterReturnsTrue)) {
            const std::filesystem::path requested{ConvertString(std::string(directPath_.data()))};
            pendingNavigate_ =
                ContentLocation{rootIndex_, requested.is_absolute() ? requested : active.catalog->Root() / requested};
        }
        ImGui::EndPopup();
    }
    if (button("filter", "cb.filters", true, !state_.filters.empty())) {
        ImGui::OpenPopup("ContentFilters");
    }
    if (ImGui::BeginPopup("ContentFilters")) {
        for (const auto &type : catalog.Types().Types()) {
            bool enabled = state_.filters.contains(type.labelKey);
            if (ImGui::Checkbox(context.text.Text(type.labelKey).c_str(), &enabled)) {
                if (enabled) {
                    state_.filters.insert(type.labelKey);
                } else {
                    state_.filters.erase(type.labelKey);
                }
                Persist();
            }
        }
        if (ImGui::Button(context.text.Text("cb.clearFilters").c_str())) {
            state_.filters.clear();
            Persist();
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    UI::EditorIcon("search");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(std::max(100.f, ImGui::GetContentRegionAvail().x - 220));
    UI::SearchBar(
        "ContentBrowserSearch", state_.search, context.text.Text("cb.searchHint").c_str(),
        std::max(80.f, ImGui::GetContentRegionAvail().x - (ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.x * 2 +
                                                           ImGui::GetStyle().ItemSpacing.x) *
                                                              5));
    const auto query = ContentSearchParser::Parse(state_.search);
    searchError_ = query.error;
    ImGui::SameLine();
    if (button("view_grid", "cb.grid", true, state_.view == ContentViewMode::Grid)) {
        state_.view = ContentViewMode::Grid;
        Persist();
    }
    ImGui::SameLine();
    if (button("view_list", "cb.list", true, state_.view == ContentViewMode::List)) {
        state_.view = ContentViewMode::List;
        Persist();
    }
    ImGui::SameLine();
    if (button("view_details", "cb.details", true, state_.view == ContentViewMode::Details)) {
        state_.view = ContentViewMode::Details;
        Persist();
    }
    ImGui::SameLine();
    if (button(state_.descending ? "sort_descending" : "sort_ascending", "cb.reverseSort")) {
        state_.descending = !state_.descending;
        Persist();
    }
    ImGui::SameLine();
    if (button("settings", "cb.settings")) {
        ImGui::OpenPopup("ContentSettings");
    }
    if (ImGui::BeginPopup("ContentSettings")) {
        bool changed = false;
        int sort = static_cast<int>(state_.sort);
        const char *sorts[] = {"Name", "Type", "Size", "Modified"};
        if (ImGui::Combo(context.text.Text("cb.sort").c_str(), &sort, sorts, 4)) {
            state_.sort = static_cast<ContentSort>(sort);
            changed = true;
        }
        changed |= ImGui::Checkbox(context.text.Text("cb.engineContent").c_str(), &showEngine_);
        changed |= ImGui::Checkbox(context.text.Text("cb.pluginContent").c_str(), &showPlugins_);
        changed |= ImGui::Checkbox(context.text.Text("cb.recursive").c_str(), &recursive_);
        bool automatic = catalog.AutomaticRefresh();
        if (ImGui::Checkbox(context.text.Text("cb.automatic").c_str(), &automatic)) {
            for (auto &value : roots_) {
                if (!value.engine && !value.plugin) {
                    value.catalog->SetAutomaticRefresh(automatic);
                }
            }
            if (settings_) {
                settings_->user.contentBrowser[AssetCatalog::Utf8(assets_.Root()) + ".monitor"] =
                    automatic ? "on" : "off";
                if (!settings_->SaveUser()) {
                    error_ = "Could not save monitor preferences.";
                }
            }
        }
        if (!catalog.WatchStatus().empty()) {
            ImGui::TextWrapped("%s", catalog.WatchStatus().c_str());
        }
        changed |= ImGui::Checkbox(context.text.Text("cb.sources").c_str(), &state_.showSources);
        changed |= ImGui::Checkbox(context.text.Text("cb.folders").c_str(), &state_.showFolders);
        changed |= ImGui::Checkbox(context.text.Text("cb.sourceFiles").c_str(), &state_.showSourceFiles);
        for (const auto &type : catalog.Types().Types()) {
            if (!type.source) {
                continue;
            }
            bool shown = !state_.hiddenTypes.contains(type.labelKey);
            if (ImGui::Checkbox(context.text.Text(type.labelKey).c_str(), &shown)) {
                if (shown) {
                    state_.hiddenTypes.erase(type.labelKey);
                } else {
                    state_.hiddenTypes.insert(type.labelKey);
                }
                changed = true;
            }
        }
        changed |= ImGui::Checkbox(context.text.Text("cb.folderFirst").c_str(), &state_.folderFirst);
        changed |=
            ImGui::SliderFloat(context.text.Text("cb.thumbnailSize").c_str(), &state_.thumbnailSize, 40, 160, "%.0f");
        if (changed) {
            Persist();
        }
        ImGui::TextWrapped(
            "%s", context.text.Text(catalog.AutomaticRefresh() ? "cb.autoDescription" : "cb.manualRefresh").c_str());
        ImGui::EndPopup();
    }
    if (!searchError_.empty()) {
        ImGui::TextWrapped("%s", searchError_.c_str());
    }
    if (!error_.empty()) {
        UI::EditorIcon("warning");
        ImGui::SameLine();
        ImGui::TextWrapped("%s", error_.c_str());
        if (ImGui::SmallButton(context.text.Text("cb.dismiss").c_str())) {
            error_.clear();
        }
    }
}
void ContentBrowserPanel::DrawSources(const EditorContext &context) {
    UI::EditorIcon("favorite");
    ImGui::SameLine();
    ImGui::TextUnformatted(context.text.Text("cb.favorites").c_str());
    for (const auto &path : state_.favorites) {
        ImGui::PushID(AssetCatalog::Utf8(path).c_str());
        UI::EditorIcon("favorite");
        ImGui::SameLine();
        if (ImGui::Selectable(AssetCatalog::Utf8(path.filename()).c_str())) {
            for (size_t i = 0; i < roots_.size(); ++i) {
                const auto relative = path.lexically_relative(roots_[i].catalog->Root());
                if (relative.empty() || relative.is_absolute() || *relative.begin() == "..") {
                    continue;
                }
                if (roots_[i].catalog->Navigate(path)) {
                    pendingNavigate_ = ContentLocation{i, path};
                } else {
                    Open(path);
                }
                break;
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", AssetCatalog::Utf8(path).c_str());
        }
        if (ImGui::BeginPopupContextItem("FavoriteActions")) {
            if (UI::IconMenuItem("favorite", context.text.Text("cb.removeFavorite").c_str())) {
                // 描画中のvectorを変更しない。削除候補を値で保存しloop後に適用する。
                favoriteRemoval_ = path;
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    if (favoriteRemoval_) {
        state_.ToggleFavorite(*favoriteRemoval_);
        favoriteRemoval_.reset();
        Persist();
    }
    ImGui::Separator();
    UI::EditorIcon("project");
    ImGui::SameLine();
    ImGui::TextUnformatted(context.text.Text("cb.project").c_str());
    UI::SearchBar("ContentFolderSearch", state_.folderSearch, context.text.Text("cb.folderSearch").c_str());
    std::function<void(size_t, const std::filesystem::path &, size_t)> tree;
    tree = [&](size_t root, const std::filesystem::path &path, size_t depth) {
        auto &catalog = *roots_[root].catalog;
        const auto key = AssetCatalog::Utf8(path);
        ImGui::PushID(static_cast<int>(root));
        ImGui::PushID(key.c_str());
        const bool top = path == catalog.Root();
        const auto name = top ? context.text.Text(roots_[root].label) : AssetCatalog::Utf8(path.filename());
        // 検索しても未展開の全treeを走査しない。既に表示した子の名前を絞り込む。
        const auto flags =
            ImGuiTreeNodeFlags_OpenOnArrow | (top ? ImGuiTreeNodeFlags_DefaultOpen : 0) |
            (std::ranges::find(folders_, ContentLocation{root, path}) != folders_.end() ? ImGuiTreeNodeFlags_Selected
                                                                                        : 0);
        const bool open = ImGui::TreeNodeEx("folder", flags, "    %s", name.c_str());
        const auto start = ImGui::GetItemRectMin();
        UI::DrawEditorIcon(open ? "folder_open" : "folder", {start.x + ImGui::GetTreeNodeToLabelSpacing(), start.y + 1},
                           ImGui::GetFontSize());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            ChooseFolder(root, path, ImGui::GetIO().KeyCtrl);
        }
        FolderDrop(context, path);
        if (ImGui::BeginDragDropSource()) {
            std::vector<std::filesystem::path> paths;
            if (std::ranges::find(folders_, ContentLocation{root, path}) != folders_.end()) {
                for (const auto &folder : folders_) {
                    paths.push_back(folder.path);
                }
            } else {
                paths.push_back(path);
            }
            const auto payload = ContentDragPayload::Encode(paths);
            if (payload) {
                ImGui::SetDragDropPayload("NEXUS_CONTENT_ITEMS", payload->data(), payload->size());
            }
            ImGui::Text("%zu folders", paths.size());
            ImGui::EndDragDropSource();
        }

        if (ImGui::BeginPopupContextItem("FolderCreateActions")) {
            DrawCreateMenu(context, path);
            if (UI::IconMenuItem("favorite", context.text.Text("cb.toggleFavorite").c_str())) {
                state_.ToggleFavorite(path);
                Persist();
            }
            ImGui::EndPopup();
        }
        if (open) {
            if (depth < 16) {
                for (const auto &entry : *catalog.Snapshot(path)) {
                    if (entry.directory &&
                        (state_.folderSearch.empty() || entry.name.find(state_.folderSearch) != std::string::npos)) {
                        tree(root, entry.path, depth + 1);
                    }
                }
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
        ImGui::PopID();
    };
    for (size_t i = 0; i < roots_.size(); ++i) {
        if ((roots_[i].engine && !showEngine_) || (roots_[i].plugin && !showPlugins_)) {
            continue;
        }
        tree(i, roots_[i].catalog->Root(), 0);
    }
    DrawLibrary(context);
}
} // namespace NexusEngine
