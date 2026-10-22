#include "DetailsPanel.h"
#include "Editor/Core/EditorSession.h"
#include "Editor/Services/PropertyEditService.h"
#include "Editor/Services/SceneEditService.h"
#include "Editor/Services/LocalizationService.h"
#include "Editor/Gui/ReflectedPropertyEditor.h"
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
    ImGui::BeginDisabled(context.session.IsPlaying());
    if(ImGui::Button(context.text.Text("edit.addComponent").c_str())) ImGui::OpenPopup("AddComponent");
    if(ImGui::BeginPopup("AddComponent")) {
        for(const auto& [typeId, type] : context.types.Types()) {
            if(!type.add) continue;
            bool missing = false;
            for(auto selected : context.selection.All()) missing |= !type.contains(level->FindEntity(selected));
            if(ImGui::MenuItem(context.text.Label(type.labelKey, typeId).c_str(), nullptr, false, missing)) {
                context.sceneEdits.AddComponent(type);
                // World交換後に古いEntityを参照しない。
                ImGui::EndPopup(); ImGui::EndDisabled(); ImGui::PopID(); return;
            }
        }
        ImGui::EndPopup();
    }
    ImGui::EndDisabled();
    // Reflection記述のみ反復する。新Component対応でPanelを変更しない。
    for(const auto& [typeId, type] : context.types.Types()) if(type.contains(entity)) {
        ImGui::PushID(typeId.c_str());
        if(ImGui::CollapsingHeader(context.text.Label(type.labelKey, typeId).c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            for(const auto& descriptor : type.properties)
                DrawReflectedProperty(context, typeId, descriptor.id);
        }
        ImGui::PopID();
    }
    ImGui::PopID();
}
}
