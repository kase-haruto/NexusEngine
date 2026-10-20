#include "DetailsPanel.h"
#include "Editor/Core/EditorSession.h"
#include "Editor/Services/PropertyEditService.h"
#include "Editor/Services/LocalizationService.h"
#include "Editor/Gui/PropertyDrawerRegistry.h"
#include "Foundation/Logging/Logger.h"
#include "ThirdParty/imgui/imgui.h"

namespace NexusEngine {
void DetailsPanel::Draw(const EditorContext& context) {
    auto* level = context.session.GetLevel();
    auto id = context.selection.Primary();
    auto entity = level ? level->FindEntity(id) : Entity{};
    if(!entity.IsAlive()) { ImGui::TextUnformatted(context.text.Text("selection.empty").c_str()); return; }
    if(context.selection.All().size() > 1) ImGui::TextUnformatted(context.text.Text("selection.multiple").c_str());
    // 別Entityの同じPropertyへ入力中のImGui状態を引き継がない。
    const auto entityId = std::to_string(id.high) + ":" + std::to_string(id.low);
    ImGui::PushID(entityId.c_str());
    // Reflection記述のみ反復する。新Component対応でPanelを変更しない。
    for(const auto& [typeId, type] : context.types.Types()) if(type.contains(entity)) {
        ImGui::PushID(typeId.c_str());
        if(ImGui::CollapsingHeader(context.text.Label(type.labelKey, typeId).c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            for(const auto& descriptor : type.properties) {
                PropertyHandle handle{id, typeId, descriptor.id};
                if(auto value = context.properties.Read(handle)) {
                    std::vector<PropertyHandle> handles;
                    bool common = true, mixed = false;
                    for(auto selected : context.selection.All()) {
                        PropertyHandle target{selected, typeId, descriptor.id};
                        auto other = context.properties.Read(target);
                        if(!other) { common = false; break; }
                        mixed |= *other != *value; handles.push_back(std::move(target));
                    }
                    if(!common) continue;
                    ImGui::PushID(descriptor.id.c_str());
                    const auto label = context.text.Text(descriptor.labelKey);
                    UI::ItemOptions options;
                    options.id = UI::ItemId::FromString(descriptor.id);
                    options.label = label;
                    options.enabled = static_cast<bool>(descriptor.set);
                    const auto result = context.drawers.Draw(options, *value, descriptor, context.text);
                    if(mixed) { ImGui::SameLine(); ImGui::TextDisabled("%s", context.text.Text("property.mixed").c_str()); }
                    if(UI::HasStatus(result.status, UI::ItemStatus::Activated)) context.properties.Begin(handles);
                    if(result.Changed() && !context.properties.Preview(handles, *value))
                        NEXUS_LOG_WARNING("Details", "Property validation rejected an edit.");
                    if(result.EditFinished()) context.properties.Commit();
                    ImGui::PopID();
                }
            }
        }
        ImGui::PopID();
    }
    ImGui::PopID();
}
}
