#include "ReflectedPropertyEditor.h"
#include "Editor/Core/EditorSession.h"
#include "Editor/Services/PropertyEditService.h"
#include "Editor/Gui/PropertyDrawerRegistry.h"
#include "Editor/Gui/Widgets/AssetPropertyWidget.h"
#include "Foundation/Logging/Logger.h"
#include "ThirdParty/imgui/imgui.h"
#include <algorithm>

namespace NexusEngine {
////////////////////////////////////////////////////////////////////////////////////////////
// 値の解決から確定まで共通化し、複数Panelで同じUndo・検証・Asset Drop経路を使う。
////////////////////////////////////////////////////////////////////////////////////////////
void DrawReflectedProperty(const EditorContext& context, std::string_view typeId, std::string_view propertyId,
    bool primaryOnly) {
    const auto* type = context.types.Find(typeId);
    if(!type) return;
    const auto found = std::ranges::find(type->properties, propertyId, &PropertyDescriptor::id);
    if(found == type->properties.end()) return;
    const auto& descriptor = *found;
    const auto id = context.selection.Primary();
    const PropertyHandle handle{id, std::string(typeId), std::string(propertyId)};
    auto value = context.properties.Read(handle);
    if(!value) return;
    std::vector<PropertyHandle> handles;
    bool mixed = false;
    for(auto selected : context.selection.All()) {
        if(primaryOnly && selected != id) continue;
        PropertyHandle target{selected, handle.type, handle.property};
        const auto other = context.properties.Read(target);
        if(!other) return;
        mixed |= *other != *value; handles.push_back(std::move(target));
    }
    std::string mesh, nodeProperty;
    int node = -1;
    for(const auto& property : type->properties) {
        if(property.kind != PropertyKind::MeshAsset && property.kind != PropertyKind::MeshNode) continue;
        auto current = context.properties.Read({id, handle.type, property.id});
        if(!current) continue;
        if(property.kind == PropertyKind::MeshAsset) if(auto* path = std::get_if<std::string>(&*current)) mesh = *path;
        if(property.kind == PropertyKind::MeshNode) if(auto* index = std::get_if<int>(&*current)) { node = *index; nodeProperty = property.id; }
    }
    ImGui::PushID(handle.type.c_str()); ImGui::PushID(handle.property.c_str());
    ImGui::BeginDisabled(context.session.IsPlaying());
    UI::ItemOptions options;
    const auto label = context.text.Text(descriptor.labelKey);
    options.id = UI::ItemId::FromString(handle.property);
    options.label = label;
    options.enabled = static_cast<bool>(found->set);
    options.size.x = std::max(40.0f, ImGui::GetContentRegionAvail().x * .54f);
    const bool asset = descriptor.kind == PropertyKind::MeshAsset || descriptor.kind == PropertyKind::TextureAsset || descriptor.kind == PropertyKind::MeshNode;
    // Stable columns align labels across independently drawn reflected properties.
    // Asset cards keep their full-width layout for previews and drop targets.
    const bool propertyRow = !asset && ImGui::BeginTable("##PropertyRow", 2,
        ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings);
    if(propertyRow) {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.36f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.64f);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label.c_str());
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", label.c_str());
        ImGui::TableSetColumnIndex(1);
        options.label = {};
        options.size.x = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    }
    const auto result = asset && context.assets ? context.assets->Draw(options, *value, descriptor, context.text, mesh, node) :
        context.drawers.Draw(options, *value, descriptor, context.text);
    if(propertyRow) ImGui::EndTable();
    if(result.Changed() && descriptor.kind == PropertyKind::MeshAsset && !nodeProperty.empty()) {
        std::vector<PropertyValue> values(handles.size(), *value);
        const auto original = handles;
        for(const auto& target : original) { handles.push_back({target.entity, target.type, nodeProperty}); values.push_back(-1); }
        if(context.properties.PreviewValues(handles, values)) context.properties.Commit(); else context.properties.Cancel();
    } else {
        if(mixed) { ImGui::SameLine(); ImGui::TextDisabled("%s", context.text.Text("property.mixed").c_str()); }
        if(UI::HasStatus(result.status, UI::ItemStatus::Activated)) context.properties.Begin(handles);
        if(result.Changed() && !context.properties.Preview(handles, *value))
            NEXUS_LOG_WARNING("PropertyEditor", "Property validation rejected an edit.");
        if(result.EditFinished()) context.properties.Commit();
    }
    ImGui::EndDisabled(); ImGui::PopID(); ImGui::PopID();
}
}
