#include "AddComponentWindow.h"
#include "Editor/Gui/Panels/IEditorPanel.h"
#include "Editor/Gui/Widgets/SearchBar.h"
#include "Editor/Gui/Widgets/EditorIconButton.h"
#include "Editor/Services/EditorSettings.h"
#include "Editor/Services/SceneEditService.h"
#include "Editor/Services/LocalizationService.h"
#include "Editor/Core/EditorSession.h"
#include "ThirdParty/imgui/imgui_internal.h"
#include <algorithm>

namespace NexusEngine {
AddComponentWindow::AddComponentWindow(EditorSettingsStore* settings) : settings_(settings) {
}
std::vector<std::string>& AddComponentWindow::Favorites() {
    return settings_ ? settings_->user.componentFavorites : localFavorites_;
}
void AddComponentWindow::ToggleFavorite(const std::string& id) {
    auto& favorites = Favorites();
    const auto found = std::ranges::find(favorites, id);
    if(found != favorites.end()) {
        favorites.erase(found);
    } else if(favorites.size() < 256) {
        favorites.push_back(id);
    }
    if(settings_ && !settings_->SaveUser()) {
        error_ = "settings.saveFailed";
    }
}
void AddComponentWindow::Open(const EditorContext& context) {
    if(context.session.IsPlaying() || context.selection.All().empty()) {
        return;
    }
    targets_.assign(context.selection.All().begin(), context.selection.All().end());
    levelGeneration_ = context.session.Generation();
    search_.clear();
    category_.clear();
    favoritesOnly_ = false;
    error_.clear();
    focusSearch_ = true;
    // Detailsの選択Entity用PushIDから切り離し、選択変更時も同じPopupを閉じられる。
    popupId_ = ImHashStr("AddComponentBrowser", 0, ImGui::GetCurrentWindow()->ID);
    ImGui::OpenPopupEx(popupId_, ImGuiPopupFlags_None);
}
bool AddComponentWindow::DrawCard(const ComponentBrowserEntry& entry, const ComponentAddAvailability& availability,
    const EditorContext& context) {
    const float font = ImGui::GetFontSize();
    const float height = font * 4.5f;
    const float favoriteWidth = font * 2;
    const auto width = std::max(font * 8, ImGui::GetContentRegionAvail().x - favoriteWidth - ImGui::GetStyle().ItemSpacing.x);
    const auto start = ImGui::GetCursorScreenPos();
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(start, {start.x + width, start.y + height}, ImGui::GetColorU32(ImGuiCol_FrameBg), 5);
    ImGui::BeginDisabled(!availability.enabled);
    const bool clicked = ImGui::Selectable("##ComponentCard", false, ImGuiSelectableFlags_NoAutoClosePopups, {width, height});
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    draw->AddRect(start, {start.x + width, start.y + height}, ImGui::GetColorU32(hovered ? ImGuiCol_HeaderHovered : ImGuiCol_Border), 5);
    const float iconSide = font * 1.6f;
    const ImVec2 iconMin{start.x + 9, start.y + 10};
    const auto* icon = UI::FindEditorIcon(entry.icon);
    if(!icon) {
        icon = UI::FindEditorIcon("details.Object");
    }
    if(icon) {
        draw->AddImage(*icon, iconMin, {iconMin.x + iconSide, iconMin.y + iconSide}, {0, 0}, {1, 1}, ImGui::GetColorU32(ImGuiCol_Text));
    } else {
        // 画像未配置の型にも同じ抽象アイコン領域を提供する。GPU資源は生成しない。
        const auto color = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        draw->AddRect(iconMin, {iconMin.x + iconSide, iconMin.y + iconSide}, color, 3.f, 2.f, ImDrawFlags_None);
        draw->AddLine(iconMin, {iconMin.x + iconSide, iconMin.y + iconSide}, color);
        draw->AddLine({iconMin.x + iconSide, iconMin.y}, {iconMin.x, iconMin.y + iconSide}, color);
    }
    const float textX = iconMin.x + iconSide + 10;
    const auto clipMax = ImVec2{start.x + width - 8, start.y + height};
    draw->PushClipRect({textX, start.y}, clipMax, true);
    draw->AddText({textX, start.y + 7}, ImGui::GetColorU32(ImGuiCol_Text), entry.name.c_str());
    draw->AddText({textX, start.y + font + 10}, ImGui::GetColorU32(ImGuiCol_TextDisabled), entry.description.c_str());
    const auto footer = availability.enabled ? entry.originName + " / " + entry.categoryName : context.text.Text(availability.reason);
    draw->AddText({textX, start.y + font * 2 + 14}, ImGui::GetColorU32(ImGuiCol_TextDisabled), footer.c_str());
    draw->PopClipRect();
    ImGui::EndDisabled();
    if(hovered) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(font * 25);
        ImGui::TextUnformatted(entry.name.c_str());
        ImGui::TextWrapped("%s", entry.description.c_str());
        ImGui::TextDisabled("%s", entry.id.c_str());
        if(!availability.enabled) {
            ImGui::TextWrapped("%s", context.text.Text(availability.reason).c_str());
        }
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::SameLine();
    const bool favorite = std::ranges::find(Favorites(), entry.id) != Favorites().end();
    if(ImGui::Button(favorite ? "★###Favorite" : "☆###Favorite", {favoriteWidth, height})) {
        ToggleFavorite(entry.id);
    }
    if(ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", context.text.Text("componentBrowser.favoriteToggle").c_str());
    }
    return clicked;
}
bool AddComponentWindow::Draw(const EditorContext& context) {
    if(!popupId_ || !ImGui::IsPopupOpen(popupId_, ImGuiPopupFlags_None)) {
        return false;
    }
    const auto* viewport = ImGui::GetMainViewport();
    const float scale = ImGui::GetFontSize() / 18.f;
    ImGui::SetNextWindowSize({std::min(760.f * scale, viewport->WorkSize.x - 20),
        std::min(560.f * scale, viewport->WorkSize.y - 20)}, ImGuiCond_Appearing);
    if(!ImGui::BeginPopupEx(popupId_, ImGuiWindowFlags_NoSavedSettings)) {
        return false;
    }
    const bool sameTargets = std::ranges::equal(targets_, context.selection.All());
    const bool valid = sameTargets && levelGeneration_ == context.session.Generation() && !context.session.IsPlaying() &&
        context.session.GetLevel() && std::ranges::all_of(targets_, [&](const auto id) {
            return context.session.GetLevel()->FindEntity(id).IsAlive();
        });
    if(!valid || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return false;
    }
    ImGui::TextUnformatted(context.text.Text("edit.addComponent").c_str());
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::GetFontSize() * 2);
    if(ImGui::SmallButton("X###CloseComponentBrowser")) {
        ImGui::CloseCurrentPopup();
    }
    if(focusSearch_) {
        ImGui::SetKeyboardFocusHere();
    }
    UI::SearchBar("ComponentBrowserSearch", search_, context.text.Text("componentBrowser.search").c_str());
    if(focusSearch_ && ImGui::IsItemActive()) {
        // Popup初回のnavigation初期化を跨いでも、検索欄が実際にactiveになるまで要求する。
        focusSearch_ = false;
    }
    model_.Update(context.types, context.text);
    std::string chosen;
    if(ImGui::BeginTable("ComponentBrowserLayout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Categories", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 8.5f);
        ImGui::TableSetupColumn("Components", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::BeginChild("ComponentCategories", {0, 0});
        if(ImGui::Selectable(context.text.Label("componentBrowser.favorites", "Favorites").c_str(), favoritesOnly_)) {
            favoritesOnly_ = true;
            category_.clear();
        }
        ImGui::Separator();
        if(ImGui::Selectable(context.text.Label("componentBrowser.all", "All").c_str(), category_.empty() && !favoritesOnly_)) {
            category_.clear();
            favoritesOnly_ = false;
        }
        for(const auto& [id, name] : model_.Categories()) {
            if(ImGui::Selectable((name + "###" + id).c_str(), category_ == id && !favoritesOnly_)) {
                category_ = id;
                favoritesOnly_ = false;
            }
        }
        ImGui::EndChild();
        ImGui::TableNextColumn();
        ImGui::BeginChild("ComponentCards", {0, 0});
        const auto filtered = model_.Filter(search_, category_, Favorites(), favoritesOnly_);
        if(filtered.empty()) {
            ImGui::TextWrapped("%s", context.text.Text("componentBrowser.empty").c_str());
        }
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(filtered.size()), ImGui::GetFontSize() * 4.5f + ImGui::GetStyle().ItemSpacing.y);
        while(clipper.Step()) {
            for(int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const auto& entry = model_.Entries()[filtered[static_cast<size_t>(row)]];
                const auto* type = context.types.Find(entry.id);
                if(!type) {
                    continue;
                }
                ImGui::PushID(entry.id.c_str());
                const auto availability = ComponentBrowserModel::Availability(context, *type, targets_);
                if(DrawCard(entry, availability, context)) {
                    chosen = entry.id;
                }
                ImGui::PopID();
            }
        }
        if(!error_.empty()) {
            ImGui::TextWrapped("%s", context.text.Text(error_).c_str());
        }
        ImGui::EndChild();
        ImGui::EndTable();
    }
    // 全child・tableを閉じてからWorldを交換し、描画中の参照を無効にしない。
    bool added = false;
    if(!chosen.empty()) {
        const auto* type = context.types.Find(chosen);
        if(type && ComponentBrowserModel::Availability(context, *type, targets_).enabled) {
            added = type->addInstance ? context.sceneEdits.AddScriptInstance(*type) : context.sceneEdits.AddComponent(*type, targets_);
        }
        if(added) {
            ImGui::CloseCurrentPopup();
        } else {
            error_ = "edit.rejected";
        }
    }
    ImGui::EndPopup();
    return added;
}
}
