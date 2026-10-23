#include "MaterialPage.h"
#include "Editor/Core/EditorSession.h"
#include "Editor/Gui/ReflectedPropertyEditor.h"
#include "Editor/Gui/Widgets/AssetPropertyWidget.h"
#include "Editor/Services/PropertyEditService.h"
#include "ThirdParty/imgui/imgui.h"

namespace NexusEngine {
namespace {
// Slot変更とSource値の初期化を一つのUndoへまとめる。共有Sourceへの書き込みは行わない。
void UseSource(const EditorContext& context, int slot, const ModelMaterialAssetData& source) {
    const auto id = context.selection.Primary();
    const std::vector<PropertyHandle> handles{{id,"PrimitiveRender","materialSlot"}, {id,"PrimitiveRender","materialBaseColor"},
        {id,"PrimitiveRender","materialMetallic"}, {id,"PrimitiveRender","materialRoughness"},
        {id,"PrimitiveRender","materialNormalScale"}, {id,"PrimitiveRender","materialEmissive"},
        {id,"PrimitiveRender","materialOcclusion"}, {id,"PrimitiveRender","materialEnabled"}};
    const std::vector<PropertyValue> values{slot, source.baseColorFactor, source.metallicFactor, source.roughnessFactor,
        source.normalScale, source.emissiveFactor, source.occlusionStrength, true};
    if(context.properties.PreviewValues(handles, values)) context.properties.Commit(); else context.properties.Cancel();
}
}
bool MaterialPage::IsAvailable(const EditorContext& context) {
    const auto* type = context.types.Find("PrimitiveRender");
    return type && type->contains(context.session.GetSelectedEntity());
}
////////////////////////////////////////////////////////////////////////////////////////////
// 各アクセスは永続IDから解決する。UndoによるWorld交換を跨ぐComponent参照を保持しない。
////////////////////////////////////////////////////////////////////////////////////////////
void MaterialPage::Draw(const EditorContext& context) {
    if(!IsAvailable(context)) return;
    const auto id = context.selection.Primary();
    const auto read = [&](const char* property) { return context.properties.Read({id, "PrimitiveRender", property}); };
    auto meshValue = read("meshAsset"), slotValue = read("materialSlot"), enabledValue = read("materialEnabled");
    if(!meshValue || !slotValue || !enabledValue) return;
    const auto& mesh = std::get<std::string>(*meshValue);
    int slot = std::get<int>(*slotValue);
    const bool enabled = std::get<bool>(*enabledValue);
    const auto* source = context.assets ? context.assets->SourceModel(mesh) : nullptr;
    const bool validSlot = source && slot >= 0 && static_cast<size_t>(slot) < source->materials.size();
    ImGui::TextWrapped("%s", context.text.Text("material.instanceHelp").c_str());
    if(context.selection.All().size() > 1) ImGui::TextWrapped("%s", context.text.Text("material.primaryOnly").c_str());
    ImGui::BeginDisabled(context.session.IsPlaying());
    if(ImGui::CollapsingHeader(context.text.Label("material.slots", "Slots").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto current = validSlot ? source->materials[slot].name + " [" + std::to_string(slot) + "]" : context.text.Text("asset.missing");
        ImGui::SetNextItemWidth(-1);
        if(ImGui::BeginCombo("##MaterialSlot", current.c_str())) {
            if(source) for(size_t index = 0; index < source->materials.size(); ++index) {
                const auto label = source->materials[index].name + " [" + std::to_string(index) + "]###" + std::to_string(index);
                if(ImGui::Selectable(label.c_str(), slot == static_cast<int>(index))) UseSource(context, static_cast<int>(index), source->materials[index]);
            }
            ImGui::EndCombo();
        }
        if(!source) ImGui::TextWrapped("%s", context.text.Text("material.sourceUnavailable").c_str());
        if(validSlot) {
            if(ImGui::Button(context.text.Label(enabled ? "material.resetSource" : "material.createOverride", "UseSource").c_str()))
                UseSource(context, slot, source->materials[slot]);
            ImGui::TextWrapped("%s", context.text.Text("material.embeddedMaps").c_str());
        }
        DrawReflectedProperty(context, "PrimitiveRender", "materialEnabled", true);
    }
    ImGui::BeginDisabled(!enabled);
    if(ImGui::CollapsingHeader(context.text.Label("material.surface", "Surface").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        for(const char* property : {"materialBaseColor", "materialMetallic", "materialRoughness", "materialNormalScale", "materialEmissive", "materialOcclusion"})
            DrawReflectedProperty(context, "PrimitiveRender", property, true);
        ImGui::PushTextWrapPos(); ImGui::TextDisabled("%s", context.text.Text("material.ranges").c_str()); ImGui::PopTextWrapPos();
    }
    ImGui::EndDisabled();
    if(ImGui::CollapsingHeader(context.text.Label("material.textures", "Textures").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextWrapped("%s", context.text.Text("material.textureScope").c_str());
        DrawReflectedProperty(context, "PrimitiveRender", "baseColorTexture", true);
        DrawReflectedProperty(context, "PrimitiveRender", "tint", true);
    }
    ImGui::EndDisabled();
}
}
