#include "PropertyCategoryView.h"
#include "Editor/Services/PropertyEditService.h"
#include "Editor/Services/LocalizationService.h"
#include "Editor/Core/EditorSession.h"
#include <algorithm>

namespace NexusEngine {
////////////////////////////////////////////////////////////////////////////////////////////
// Entityの生ポインタを保存せず、毎frameカテゴリの可用性と主選択IDを解決する。
////////////////////////////////////////////////////////////////////////////////////////////
void PropertyCategoryView::Draw(const EditorContext& context) {
    const auto available = registry_.Available(context);
    if(available.empty()) {
        activeCategoryId_.clear();
        ImGui::TextWrapped("%s", context.text.Text("properties.empty").c_str()); return;
    }
    if(std::ranges::none_of(available, [&](auto* category) { return category->id == activeCategoryId_; }))
        activeCategoryId_ = available.front()->id;
    const auto id = context.selection.Primary();
    const auto scope = std::to_string(context.session.Generation()) + ":" + std::to_string(id.high) + ":" + std::to_string(id.low);
    ImGui::PushID(scope.c_str());
    const float side = ImGui::GetFrameHeight() * 1.25f;
    if(ImGui::BeginTable("PropertiesLayout", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Navigation", ImGuiTableColumnFlags_WidthFixed, side + ImGui::GetStyle().ItemSpacing.x);
        ImGui::TableSetupColumn("Page", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
        if(ImGui::BeginChild("CategoryNavigation", {0, 0})) {
            for(const auto* category : available) {
                ImGui::PushID(category->id.c_str());
                const bool selected = category->id == activeCategoryId_;
                const auto origin = ImGui::GetCursorScreenPos();
                if(selected) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
                if(ImGui::Button("##category", {side, side})) {
                    context.properties.Commit(); activeCategoryId_ = category->id;
                }
                if(selected) ImGui::PopStyleColor();
                if(category->drawIcon) category->drawIcon(*ImGui::GetWindowDrawList(), origin, side, ImGui::GetColorU32(ImGuiCol_Text));
                else ImGui::GetWindowDrawList()->AddCircle({origin.x + side / 2, origin.y + side / 2}, side * .22f, ImGui::GetColorU32(ImGuiCol_Text));
                if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", context.text.Text(category->labelKey).c_str());
                ImGui::PopID();
            }
        }
        ImGui::EndChild(); ImGui::TableSetColumnIndex(1);
        const auto pageId = "PropertyPage/" + activeCategoryId_;
        if(ImGui::BeginChild(pageId.c_str(), {0, 0})) {
            for(const auto* category : available) if(category->id == activeCategoryId_) {
                ImGui::PushID(category->id.c_str());
                ImGui::TextUnformatted(context.text.Text(category->labelKey).c_str()); ImGui::Separator();
                category->drawPage(context);
                ImGui::PopID(); break;
            }
        }
        ImGui::EndChild(); ImGui::EndTable();
    }
    ImGui::PopID();
}
}
