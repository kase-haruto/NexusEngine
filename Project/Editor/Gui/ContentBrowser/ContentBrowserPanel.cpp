#include "ContentBrowserPanel.h"
#include "Editor/Gui/Icons/EditorIcons.h"
#include "Editor/Gui/Widgets/AssetPropertyWidget.h"
#include "Editor/Services/EditorSettings.h"
#include "Editor/Services/LocalizationService.h"
#include "Editor/Services/SceneEditService.h"
#include "Editor/Core/EditorSession.h"
#include "Foundation/Logging/Logger.h"
#include "Editor/Services/ContentDragPayload.h"
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <sstream>
#include <iomanip>
#include "ThirdParty/nlohmann/json.hpp"
#include <chrono>

namespace NexusEngine {
ContentBrowserPanel::ContentBrowserPanel(AssetCatalog &assets, std::function<void(const std::filesystem::path &)> open,
                                         AssetCatalog *sources, SourceCreationService *creation,
                                         EditorSettingsStore *settings, AssetCatalog *levels,
                                         std::shared_ptr<ContentBrowserServices> services, std::string id)
    : creation_(creation), assets_(assets), sources_(sources), settings_(settings), open_(std::move(open)) {
    services_ = std::move(services);
    id_ = std::move(id);
    titleKey_ = id_ == "ContentBrowser" ? "panel.content" : "panel.content" + id_.substr(14);
    const auto projectKey = AssetCatalog::Utf8(assets.Root());
    settingsKey_ = projectKey + (id_ == "ContentBrowser" ? "" : "#" + id_);
    if (!services_) {
        services_ = std::make_shared<ContentBrowserServices>();
        services_->AddRoot("Content", "browser.assets", "content", assets);
        if (sources) {
            services_->AddRoot("Source", "browser.source", "source", *sources, false, true);
        }
        if (levels) {
            services_->AddRoot("Levels", "asset.level", "asset_level", *levels);
        }
        services_->library.Initialize(settings, projectKey,
                                      settings ? settings->ProjectDirectory() : std::filesystem::path{});
    }
    services_->settings = settings_;
    fileRevision_ = services_->fileRevision;
    SyncRoots();
    state_.Load(settings_, settingsKey_);
    state_.history.Visit({0, roots_[0].current});
    folders_.push_back({0, roots_[0].current});
    if (settings_) {
        const auto preferences = settings_->user.contentBrowser.find(settingsKey_ + ".extras");
        if (preferences != settings_->user.contentBrowser.end()) {
            try {
                const auto value = nlohmann::json::parse(preferences->second);
                recursive_ = value.value("recursive", false);
                showEngine_ = value.value("engine", false);
                showPlugins_ = value.value("plugins", true);
            } catch (const std::exception &) {
            }
        }
    }
    if (creation_) {
        sourceRevision_ = creation_->Revision();
    }
}
void ContentBrowserPanel::RegisterRoot(std::string labelKey, std::string icon, AssetCatalog &catalog) {
    services_->AddRoot(labelKey, labelKey, icon, catalog);
    SyncRoots();
}
std::array<float, 2> ContentBrowserPanel::InitialSize() const noexcept {
    // 新規のfloating BrowserでもToolbarとSourcesを操作できる幅を確保する。
    return {900, 540};
}
std::string_view ContentBrowserPanel::Id() const noexcept {
    return id_;
}
std::string_view ContentBrowserPanel::TitleKey() const noexcept {
    return titleKey_;
}
void ContentBrowserPanel::Persist() {
    if (settings_) {
        settings_->user.contentBrowser[settingsKey_ + ".extras"] =
            nlohmann::json{{"recursive", recursive_}, {"engine", showEngine_}, {"plugins", showPlugins_}}.dump();
    }

    if (!state_.Save(settings_, settingsKey_)) {
        error_ = "Could not save Content Browser preferences.";
    }
}
bool ContentBrowserPanel::Navigate(size_t root, const std::filesystem::path &path, bool history) {
    if (root >= roots_.size() || !roots_[root].catalog->Navigate(path)) {
        error_ = "The folder is unavailable or outside this project's registered roots. Refresh the listing.";
        return false;
    }
    rootIndex_ = root;
    roots_[root].current = roots_[root].catalog->Current();
    virtualView_.clear();
    collectionId_.clear();
    folders_ = {{root, roots_[root].current}};
    state_.selection.clear();
    if (history) {
        state_.history.Visit({root, roots_[root].catalog->Current()});
    }
    return true;
}
void ContentBrowserPanel::Open(const std::filesystem::path &path) {
    const auto selectedRoot = services_->RootFor(path);
    const auto &handler = (selectedRoot ? services_->roots[*selectedRoot].catalog : roots_[rootIndex_].catalog)
                              ->Types()
                              .Classify(path, false);
    const auto reference = services_->Reference(path);
    if (reference) {
        static_cast<void>(services_->library.Opened(*reference));
    }
    if (handler.open) {
        handler.open(path);
    } else if (open_) {
        open_(path);
    }
}
void ContentBrowserPanel::DrawAssetView(const EditorContext &context) {
    auto &catalog = *roots_[rootIndex_].catalog;
    const auto snapshot = Listing();
    std::ostringstream key;
    key << std::quoted(state_.search) << static_cast<int>(state_.sort) << state_.descending << state_.folderFirst
        << state_.showFolders << state_.showSourceFiles;
    key << "hidden:";
    for (const auto &hidden : state_.hiddenTypes) {
        key << std::quoted(hidden);
    }
    key << "filters:";
    for (const auto &filter : state_.filters) {
        key << std::quoted(filter);
    }
    if (visibleSnapshot_ != snapshot || visibleKey_ != key.str()) {
        visibleSnapshot_ = snapshot;
        visibleKey_ = key.str();
        visible_ = state_.Visible(*snapshot, catalog.Types());
        if (virtualView_ == "Recent") {
            std::map<std::filesystem::path, size_t> order;
            for (size_t index = 0; index < snapshot->size(); ++index) {
                order[(*snapshot)[index].path] = index;
            }
            std::ranges::sort(visible_, [&](const auto &a, const auto &b) {
                return order.at(a.path) < order.at(b.path);
            });
        }
        if (virtualView_ == "Modified" && visible_.size() > 1000) {
            visible_.resize(1000);
        }
    }
    ImGui::BeginChild("asset-list", {0, 0});
    const bool grid = state_.view == ContentViewMode::Grid;
    const bool details = state_.view == ContentViewMode::Details;
    const float tile = state_.thumbnailSize * ImGui::GetFontSize() / 18.f;
    const int columns = grid ? std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / (tile + 28))) : 1;
    const bool table = grid || details;
    const bool tableOpen =
        table && ImGui::BeginTable("ContentItems", grid ? columns : 4, ImGuiTableFlags_SizingStretchSame);
    if (details && tableOpen) {
        ImGui::TableSetupColumn(context.text.Text("cb.name").c_str());
        ImGui::TableSetupColumn(context.text.Text("cb.type").c_str());
        ImGui::TableSetupColumn(context.text.Text("cb.size").c_str());
        ImGui::TableSetupColumn(context.text.Text("cb.modified").c_str());
        ImGui::TableHeadersRow();
    }
    ImGuiListClipper clipper;
    const float rowHeight =
        grid ? tile + ImGui::GetTextLineHeightWithSpacing() * 2 + 12 : ImGui::GetTextLineHeightWithSpacing();
    clipper.Begin(static_cast<int>((visible_.size() + columns - 1) / columns), rowHeight);
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            for (int column = 0; column < columns; ++column) {
                const auto index = static_cast<size_t>(row * columns + column);
                if (index >= visible_.size()) {
                    break;
                }
                const auto &entry = visible_[index];
                const auto ownerRoot = services_->RootFor(entry.path);
                const auto &type =
                    (ownerRoot ? services_->roots[*ownerRoot].catalog : &catalog)->Types().Find(entry.typeKey);
                const auto path = AssetCatalog::Utf8(entry.path);
                ImGui::PushID(path.c_str());
                if (tableOpen) {
                    ImGui::TableNextColumn();
                }
                const auto start = ImGui::GetCursorScreenPos();
                const float side = grid ? tile : ImGui::GetFontSize();
                const std::string label = grid ? "###ContentItem" : "    " + entry.name + "###ContentItem";
                if (ImGui::Selectable(label.c_str(), state_.Selected(entry.path), ImGuiSelectableFlags_AllowDoubleClick,
                                      {0, grid ? rowHeight - 4 : 0})) {
                    state_.Select(visible_, index, ImGui::GetIO().KeyCtrl, ImGui::GetIO().KeyShift);
                    services_->Select(state_.selection, context.selection.Revision());
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        if (entry.directory) {
                            pendingNavigate_ = ContentLocation{rootIndex_, entry.path};
                        } else {
                            Open(entry.path);
                        }
                    }
                }
                const bool highlight = ImGui::IsItemHovered();
                const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal);
                UI::DrawEditorIcon(type.icon, {start.x, start.y + (grid ? 0 : 1)}, side, state_.Selected(entry.path));
                if (grid) {
                    // ラベルと画像の領域を分離し、selection / DnDの対象を一つのitemに保つ。
                    ImGui::GetWindowDrawList()->AddRectFilled(start, {start.x + tile, start.y + tile},
                                                              ImGui::GetColorU32(ImGuiCol_FrameBg), 3);
                    const auto image =
                        context.assets && type.preview != ContentPreview::None
                            ? context.assets->PreviewImage(path, type.preview == ContentPreview::Texture, -1)
                            : ImTextureRef{};
                    if (image._TexData || image._TexID != ImTextureID_Invalid) {
                        ImGui::GetWindowDrawList()->AddImage(image, start, {start.x + side, start.y + side});
                    } else {
                        UI::DrawEditorIcon(type.icon, start, side, state_.Selected(entry.path));
                    }
                    if (highlight || state_.Selected(entry.path)) {
                        ImGui::GetWindowDrawList()->AddRect(start, {start.x + side, start.y + side},
                                                            ImGui::GetColorU32(state_.Selected(entry.path)
                                                                                   ? ImGuiCol_HeaderActive
                                                                                   : ImGuiCol_HeaderHovered),
                                                            3.f, 2.f);
                    }
                    ImGui::GetWindowDrawList()->AddText({start.x, start.y + tile + 3},
                                                        ImGui::GetColorU32(ImGuiCol_Text), entry.name.c_str());
                    ImGui::GetWindowDrawList()->AddText(
                        {start.x, start.y + tile + 3 + ImGui::GetTextLineHeightWithSpacing()},
                        ImGui::GetColorU32(ImGuiCol_TextDisabled), context.text.Text(type.labelKey).c_str());
                }
                if (entry.missing || services_->documents.Dirty(entry.path)) {
                    UI::DrawEditorIcon(entry.missing ? "missing" : "dirty", {start.x + side - 10, start.y}, 10);
                }
                if (entry.directory) {
                    FolderDrop(context, entry.path);
                }
                if (ImGui::BeginPopupContextItem("ContentItemActions")) {
                    if (!state_.Selected(entry.path)) {
                        state_.Select(visible_, index, false, false);
                    }
                    DrawEntryActions(context, entry);
                    ImGui::EndPopup();
                }
                if (ImGui::BeginDragDropSource()) {
                    if (!state_.Selected(entry.path)) {
                        state_.Select(visible_, index, false, false);
                    }
                    if (state_.selection.size() > 1 || entry.directory) {
                        const auto payload = ContentDragPayload::Encode(state_.selection);
                        if (payload) {
                            ImGui::SetDragDropPayload("NEXUS_CONTENT_ITEMS", payload->data(), payload->size());
                        }
                        ImGui::Text("%zu items", state_.selection.size());
                    } else {
                        // 単一fileは既存Details / Animation Editorの契約を保つ。
                        ImGui::SetDragDropPayload(type.source ? "NEXUS_SOURCE_PATH" : "NEXUS_ASSET_PATH", path.c_str(),
                                                  path.size() + 1);
                        ImGui::TextUnformatted(entry.name.c_str());
                    }
                    ImGui::EndDragDropSource();
                }
                if (hovered && context.assets && type.preview != ContentPreview::None) {
                    ImGui::BeginTooltip();
                    context.assets->DrawPreview(path, type.preview == ContentPreview::Texture, -1, 160);
                    ImGui::EndTooltip();
                }
                if (details && tableOpen) {
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(context.text.Text(type.labelKey).c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%llu B", static_cast<unsigned long long>(entry.bytes));
                    ImGui::TableNextColumn();
                    const auto stamp = std::chrono::clock_cast<std::chrono::system_clock>(entry.modified);
                    const auto time = std::chrono::system_clock::to_time_t(stamp);
                    std::tm value{};
                    gmtime_s(&value, &time);
                    char text[32]{};
                    std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M", &value);
                    ImGui::TextUnformatted(text);
                }
                ImGui::PopID();
            }
        }
    }
    if (tableOpen) {
        ImGui::EndTable();
    }
    if (ImGui::BeginPopupContextWindow("ContentCreateActions",
                                       ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        DrawCreateMenu(context, roots_[rootIndex_].current);
        if (UI::IconMenuItem("folder", context.text.Text("cb.newFolder").c_str(), false,
                             !context.session.IsPlaying() && !roots_[rootIndex_].readOnly)) {
            folderRequested_ = true;
        }
        if (UI::IconMenuItem("refresh", context.text.Text("asset.refresh").c_str())) {
            catalog.Refresh();
        }
        ImGui::EndPopup();
    }
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
        ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A)) {
        state_.selection.clear();
        for (const auto &entry : visible_) {
            state_.selection.push_back(entry.path);
        }
    }
    if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseClicked(0) && !ImGui::GetIO().KeyCtrl) {
        state_.selection.clear();
    }
    ImGui::EndChild();
}
void ContentBrowserPanel::Draw(const EditorContext &context) {
    SyncRoots();
    services_->Poll();
    DrawToolbar(context);
    if (!ImGui::BeginTable("browser", state_.showSources ? 2 : 1,
                           ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
        return;
    }
    if (state_.showSources) {
        ImGui::TableSetupColumn("folders", ImGuiTableColumnFlags_WidthFixed, 190);
    }
    ImGui::TableSetupColumn("assets", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    if (state_.showSources) {
        ImGui::BeginChild("ContentSources", {0, 0});
        DrawSources(context);
        ImGui::EndChild();
        ImGui::TableNextColumn();
    }
    DrawAssetView(context);
    ImGui::EndTable();
    if (pendingNavigate_) {
        Navigate(pendingNavigate_->root, pendingNavigate_->path);
        pendingNavigate_.reset();
    }
    DrawSourceCreation(context);
    DrawDialogs(context);
    DrawLibraryDialogs(context);
    DrawImport(context);
    DrawMutation(context);
    if (attach_) {
        const auto *type = context.types.Find(*attach_);
        if (!type || !context.sceneEdits.AddComponent(*type)) {
            NEXUS_LOG_WARNING("ContentBrowser", context.text.Text("edit.rejected"));
        }
        attach_.reset();
    }
}
} // namespace NexusEngine
