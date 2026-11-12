#include "DetailsPanel.h"
#include "Editor/Core/EditorSession.h"
#include "Editor/Services/PropertyEditService.h"
#include "Editor/Services/SceneEditService.h"
#include "Editor/Services/LocalizationService.h"
#include "Editor/Gui/ReflectedPropertyEditor.h"
#include "Editor/Gui/Widgets/SourceAttachment.h"
#include "Editor/Gui/Widgets/SearchBar.h"
#include "Tools/Project/SourceCodeEditor.h"
#include "ThirdParty/imgui/imgui.h"
#include <algorithm>

namespace NexusEngine {
////////////////////////////////////////////////////////////////////////////////////////////
// 名前とComponent追加を常時表示し、設定項目は登録済みカテゴリへ委譲する。
////////////////////////////////////////////////////////////////////////////////////////////
void DetailsPanel::Draw(const EditorContext& context) {
    auto* level = context.session.GetLevel();
    auto id = context.selection.Primary();
    auto entity = level ? level->FindEntity(id) : Entity{};
    if(!entity.IsAlive()) { categories_.Draw(context); return; }
    if(context.selection.All().size() > 1) ImGui::TextUnformatted(context.text.Text("selection.multiple").c_str());
    // 別Entityの同じPropertyへ入力中のImGui状態を引き継がない。
    const auto entityId = std::to_string(id.high) + ":" + std::to_string(id.low);
    ImGui::PushID(entityId.c_str());
    DrawReflectedProperty(context, "Name", "name");
    // 常設drop領域にして、Componentの内部Propertyへ誤ってソースを渡さない。
    ImGui::Selectable(context.text.Label("source.drop", "SourceAttachment").c_str(), false);
    auto attach = UI::AcceptSourceAttachment(context);
    bool detach = false;
    std::string detachType;
    PersistentId detachInstance;
    bool independentAttach=false;
    const auto* scriptComponent = context.types.Find("Script");
    const bool hasScript = scriptComponent && scriptComponent->contains(entity);
    // Script参照を安定型IDから表示名へ解決する。内部IDを手入力せず、登録済み型から選択する。
    const auto selectedScript = context.text.Text("script.add");
    ImGui::BeginDisabled(context.session.IsPlaying());
    if(ImGui::BeginCombo(context.text.Label("script.reference", "ScriptReference").c_str(), selectedScript.c_str())) {
        UI::SearchBar("ScriptTypeSearch", scriptSearch_, context.text.Text("search").c_str());
        for(const auto& [typeId, type] : context.types.Types()) {
            if(!typeId.starts_with("ManagedScript:") || !type.add) continue;
            const auto label = context.text.Text(type.labelKey);
            if(!scriptSearch_.empty() && label.find(scriptSearch_) == std::string::npos) continue;
            if(ImGui::Selectable(context.text.Label(type.labelKey, typeId).c_str(), type.contains(entity))) { attach = typeId; independentAttach=true; }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    // 各Typeは同じ共通Descriptorで描画・解除する。別のC# Inspectorは作らない。
    for(const auto& [typeId, type] : context.types.Types()) {
        if(!typeId.starts_with("ManagedScript:") || !type.contains(entity)) continue;
        ImGui::PushID(typeId.c_str());
        const auto slots=type.instances ? type.instances(entity) : std::vector<PersistentId>{{}};
        for(size_t slotIndex=0;slotIndex<slots.size();++slotIndex) {
        const auto slot=slots[slotIndex];
        const auto slotKey=std::to_string(slot.high)+":"+std::to_string(slot.low);
        ImGui::PushID(slotKey.c_str());
        const auto label=context.text.Text(type.labelKey)+" #"+std::to_string(slotIndex+1);
        ImGui::TextUnformatted(label.c_str()); ImGui::SameLine();
        ImGui::BeginDisabled(context.session.IsPlaying());
        if(ImGui::SmallButton(context.text.Text("script.detach").c_str())) { detachType = typeId; detachInstance=slot; }
        ImGui::EndDisabled();
        // 開く操作だけはPlay中も利用できる。現在Project外のパスは既存検証で拒否する。
        if(context.sources) for(const auto& file : type.sourceFiles) {
            const auto candidate = std::filesystem::path(ConvertString(file));
            const auto resolved = candidate.is_absolute() ? candidate : context.sources->Root() / candidate;
            if(!SourceFileActions::Contains(context.sources->Root(), resolved)) continue;
            ImGui::SameLine();
            if(ImGui::SmallButton(context.text.Text("script.open").c_str())) {
                if(const auto opened = SourceCodeEditor::OpenFile(resolved); !opened) NEXUS_LOG_ERROR("Details", opened.error().GetMessageText());
            }
            break;
        }
        ImGui::PopID();
        }
        ImGui::PopID();
    }
    ImGui::BeginDisabled(!hasScript || context.session.IsPlaying());
    if(ImGui::Button(context.text.Label("script.detachAll", "DetachScript").c_str())) detach = true;
    ImGui::EndDisabled();
    ImGui::BeginDisabled(context.session.IsPlaying());
    if(ImGui::Button(context.text.Label("edit.addComponent", "AddComponentButton").c_str())) ImGui::OpenPopup("AddComponent");
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
    ImGui::Separator();
    // Runtime objectをUIが破棄せず、値Componentの解除を既存transactionへ委譲する。
    ImGui::BeginDisabled(context.session.IsPlaying());
    if(ImGui::Button(context.text.Label("edit.removeComponent", "RemoveValueComponentButton").c_str())) ImGui::OpenPopup("RemoveValueComponent");
    if(ImGui::BeginPopup("RemoveValueComponent")) {
        for(const auto& [typeId, type] : context.types.Types()) {
            if(!type.valueBacked || !type.remove || !type.contains) continue;
            const bool present = std::ranges::any_of(context.selection.All(), [&](PersistentId selected) { return type.contains(level->FindEntity(selected)); });
            if(ImGui::MenuItem(context.text.Label(type.labelKey, typeId).c_str(), nullptr, false, present)) {
                if(!context.sceneEdits.RemoveValueComponent(type)) NEXUS_LOG_WARNING("Details", context.text.Text("edit.rejected"));
                ImGui::EndPopup(); ImGui::EndDisabled(); ImGui::PopID(); return;
            }
        }
        ImGui::EndPopup();
    }
    ImGui::EndDisabled();
    categories_.Draw(context);
    ImGui::PopID();
    if(detach) { if(!context.sceneEdits.DetachScripts()) NEXUS_LOG_WARNING("Details", context.text.Text("edit.rejected")); return; }
    if(!detachType.empty()) {
        const auto* type = context.types.Find(detachType);
        if(!type || !context.sceneEdits.DetachScript(*type,detachInstance)) NEXUS_LOG_WARNING("Details", context.text.Text("edit.rejected"));
        return;
    }
    if(attach) {
        const auto* type = context.types.Find(*attach);
        if(independentAttach && type && type->addInstance) {
            if(!context.sceneEdits.AddScriptInstance(*type)) NEXUS_LOG_WARNING("Details",context.text.Text("edit.rejected"));
            return;
        }
        // 同じ型を再dropした場合は履歴も警告も増やさない。
        if(type && std::ranges::all_of(context.selection.All(),
            [&](PersistentId selected) { return type->contains(level->FindEntity(selected)); })) return;
        if(!type || !context.sceneEdits.AddComponent(*type))
            NEXUS_LOG_WARNING("Details", context.text.Text("edit.rejected"));
    }
}
}
