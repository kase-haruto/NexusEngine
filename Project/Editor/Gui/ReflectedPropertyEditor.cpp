#include "ReflectedPropertyEditor.h"
#include "Editor/Core/EditorSession.h"
#include "Editor/Services/PropertyEditService.h"
#include "Editor/Gui/PropertyDrawerRegistry.h"
#include "Editor/Gui/Widgets/AssetPropertyWidget.h"
#include "Foundation/Logging/Logger.h"
#include "ThirdParty/imgui/imgui.h"
#include <algorithm>
#include <cstring>
#include "Runtime/Scene/EntityReference.h"
#include "Runtime/Scene/Components/NameComponent.h"

namespace NexusEngine {
////////////////////////////////////////////////////////////////////////////////////////////
// 値の解決から確定まで共通化し、複数Panelで同じUndo・検証・Asset Drop経路を使う。
////////////////////////////////////////////////////////////////////////////////////////////
void DrawReflectedProperty(const EditorContext& context, std::string_view typeId, std::string_view propertyId,
    bool primaryOnly, PersistentId instance) {
    // 型IDの値編集はDetailsのScript pickerへ統一する。公開Propertyは従来の共通Drawerで編集する。
    if(typeId == "Script" && propertyId == "scriptType") return;
    const auto* type = context.types.Find(typeId);
    if(!type) return;
    const auto found = std::ranges::find(type->properties, propertyId, &PropertyDescriptor::id);
    if(found == type->properties.end()) return;
    const auto& descriptor = *found;
    const auto id = context.selection.Primary();
    const PropertyHandle handle{id, std::string(typeId), std::string(propertyId),instance};
    const auto unavailable = [&](PersistentId target) {
        if(instance.IsValid()) { ImGui::TextDisabled("%s",context.text.Text("property.instanceUnavailable").c_str()); return; }
        // 失敗理由は共通Descriptorから取得する。言語専用InspectorやCLR参照を追加しない。
        if(!descriptor.unavailableReason) return;
        auto* level = context.session.GetLevel();
        const auto reason = descriptor.unavailableReason(level ? level->FindEntity(target) : Entity{});
        if(!reason.empty()) ImGui::TextDisabled("%s: %s", context.text.Text(descriptor.labelKey).c_str(), reason.c_str());
    };
    auto value = context.properties.Read(handle);
    if(!value) { unavailable(id); return; }
    std::vector<PropertyHandle> handles;
    size_t occurrence=0;
    if(instance.IsValid()) {
        if(!type->instances) return;
        const auto ids=type->instances(context.session.GetSelectedEntity());
        const auto foundId=std::ranges::find(ids,instance); if(foundId==ids.end()) return;
        occurrence=static_cast<size_t>(foundId-ids.begin());
    }
    bool mixed = false;
    for(auto selected : context.selection.All()) {
        if(primaryOnly && selected != id) continue;
        PropertyHandle target{selected, handle.type, handle.property};
        if(instance.IsValid()) {
            const auto ids=type->instances(context.session.GetLevel()->FindEntity(selected));
            if(occurrence>=ids.size()) { unavailable(selected); return; }
            target.instance=ids[occurrence];
        }
        const auto other = context.properties.Read(target);
        if(!other) { unavailable(selected); return; }
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
    options.enabled = instance.IsValid() ? static_cast<bool>(found->setInstance) : static_cast<bool>(found->set);
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
    UI::ItemResult referenceResult;
    const bool reference = descriptor.kind == PropertyKind::EntityReference || descriptor.kind == PropertyKind::ComponentReference;
    if(reference) {
        // 選択肢にNative pointerを保持せず、保存とUndoの値には永続IDだけを渡す。
        auto& stored = std::get<std::string>(*value);
        auto* level = context.session.GetLevel();
        const bool componentReference = descriptor.kind == PropertyKind::ComponentReference;
        const auto separator = stored.find('|');
        const auto persistent = ParseEntityReference(std::string_view(stored).substr(0, separator));
        auto target = persistent && level ? level->FindEntity(*persistent) : Entity{};
        std::string display = stored.empty() ? "None" : "Missing: " + stored;
        if(target.IsAlive()) {
            const auto* name = target.GetComponent<NameComponent>();
            display = name ? name->name : FormatEntityReference(*persistent);
            if(componentReference) {
                display += " / " + stored.substr(separator + 1);
                const auto* targetType = context.types.Find(stored.substr(separator + 1));
                if(!targetType || !targetType->contains || !targetType->contains(target)) {
                    display += " (Missing Component)";
                }
            }
        }
        bool changed = false;
        ImGui::BeginDisabled(!options.enabled);
        ImGui::SetNextItemWidth(options.size.x);
        if(ImGui::BeginCombo("##Reference", display.c_str())) {
            if(ImGui::Selectable("None", stored.empty())) {
                stored.clear();
                changed = true;
            }
            if(level) {
                level->GetScene()->ForEachEntity([&](Entity entity) {
                    const auto* entityId = entity.GetComponent<PersistentIdComponent>();
                    if(!entityId) {
                        return;
                    }
                    const auto key = FormatEntityReference(entityId->value);
                    const auto* name = entity.GetComponent<NameComponent>();
                    const auto label = (name ? name->name : key) + "##" + key;
                    if(!componentReference) {
                        if(ImGui::Selectable(label.c_str(), stored == key)) {
                            stored = key;
                            changed = true;
                        }
                    } else if(ImGui::BeginMenu(label.c_str())) {
                        for(const auto& [typeId, type] : context.types.Types()) {
                            if(type.contains && type.contains(entity) && ImGui::Selectable(typeId.c_str(), stored == key + "|" + typeId)) {
                                stored = key + "|" + typeId;
                                changed = true;
                            }
                        }
                        ImGui::EndMenu();
                    }
                });
            }
            ImGui::EndCombo();
        }
        // OutlinerからのEntity dropはComponent参照ならTransformを初期候補にする。
        if(ImGui::BeginDragDropTarget()) {
            if(const auto* payload = ImGui::AcceptDragDropPayload("NEXUS_ENTITY")) {
                if(payload->DataSize == sizeof(PersistentId) && level) {
                    PersistentId droppedId{};
                    std::memcpy(&droppedId, payload->Data, sizeof(droppedId));
                    if(level->FindEntity(droppedId).IsAlive()) {
                        stored = FormatEntityReference(droppedId) + (componentReference ? "|Transform" : "");
                        changed = true;
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::EndDisabled();
        if(changed) {
            referenceResult.status = UI::ItemStatus::Edited | UI::ItemStatus::EditCompleted;
        }
    }
    const auto result = reference ? referenceResult : asset && context.assets ? context.assets->Draw(options, *value, descriptor, context.text, mesh, node) :
        context.drawers.Draw(options, *value, descriptor, context.text);
    if(propertyRow) ImGui::EndTable();
    if(typeId == "Collider" && propertyId == "shape") {
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("Mesh: set Collision Mesh first. Dynamic bodies use Convex Mesh.");
        ImGui::PopTextWrapPos();
    }
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
