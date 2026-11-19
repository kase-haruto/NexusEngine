#include "RemoveComponentMenu.h"
#include "Editor/Gui/Panels/IEditorPanel.h"
#include "Editor/Gui/Widgets/EditorIconButton.h"
#include "Editor/Core/EditorSession.h"
#include "Editor/Services/SceneEditService.h"
#include "Editor/Services/LocalizationService.h"
#include "Foundation/Logging/Logger.h"
#include <algorithm>
#include <optional>

namespace NexusEngine {
bool DrawRemoveComponentMenu(const EditorContext& context) {
    std::string chosen;
    std::optional<size_t> occurrence;
    ImGui::BeginDisabled(context.session.IsPlaying());
    if(ImGui::Button(context.text.Label("edit.removeComponent", "RemoveComponentButton").c_str())) {
        ImGui::OpenPopup("RemoveComponent");
    }
    if(ImGui::BeginPopup("RemoveComponent")) {
        if(context.session.IsPlaying()) {
            ImGui::CloseCurrentPopup();
        } else {
            ImGui::TextDisabled("%s", context.text.Text("component.removeHelp").c_str());
            ImGui::Separator();
            bool present = false;
            auto* level = context.session.GetLevel();
            for(const auto& [id, type] : context.types.Types()) {
                if(!level || !type.contains || type.labelKey.empty()) {
                    continue;
                }
                size_t count = 0, instanceCount = 0;
                for(const auto target : context.selection.All()) {
                    auto entity = level->FindEntity(target);
                    if(entity.IsAlive() && type.contains(entity)) {
                        ++count;
                        if(type.instances) {
                            instanceCount = std::max(instanceCount, type.instances(entity).size());
                        }
                    }
                }
                if(count == 0) {
                    continue;
                }
                present = true;
                ImGui::PushID(id.c_str());
                const auto name = context.text.Text(type.editor.displayName.empty() ? type.labelKey : type.editor.displayName);
                const bool removable = (type.add || type.addInstance) && type.remove;
                const size_t rows = type.instances ? instanceCount : 1;
                for(size_t index = 0; index < rows; ++index) {
                    ImGui::PushID(static_cast<int>(index));
                    UI::EditorInlineIcon(type.editor.icon.empty() ? "details.Object" : type.editor.icon);
                    auto label = name;
                    if(type.instances) {
                        label += " #" + std::to_string(index + 1);
                    }
                    if(context.selection.All().size() > 1) {
                        label += " [" + std::to_string(count) + "/" + std::to_string(context.selection.All().size()) + "]";
                    }
                    if(!removable) {
                        label += " (" + context.text.Text("component.removeRequired") + ")";
                    }
                    if(ImGui::MenuItem((label + "###RemoveEntry").c_str(), nullptr, false,
                        removable && (!type.instances || type.removeInstance))) {
                        chosen = id;
                        if(type.instances) {
                            occurrence = index;
                        }
                    }
                    ImGui::PopID();
                }
                ImGui::PopID();
            }
            if(!present) {
                ImGui::TextDisabled("%s", context.text.Text("component.removeEmpty").c_str());
            }
        }
        ImGui::EndPopup();
    }
    ImGui::EndDisabled();
    if(chosen.empty()) {
        return false;
    }
    // ImGui scopeを閉じてから、現在のRegistryでIDを再解決してWorldを交換する。
    const auto* type = context.types.Find(chosen);
    if(type && context.sceneEdits.RemoveComponent(*type, occurrence)) {
        return true;
    }
    NEXUS_LOG_WARNING("Details", context.text.Text("edit.rejected"));
    return false;
}
}
