#include "PanelRegistry.h"
#include "Editor/Services/LocalizationService.h"
#include "ThirdParty/imgui/imgui.h"

namespace NexusEngine {
bool PanelRegistry::Register(std::unique_ptr<IEditorPanel> panel, bool open) {
    if(!panel || panel->Id().empty()) return false;
    for(const auto& entry : entries_) if(entry.panel->Id() == panel->Id()) return false;
    entries_.push_back({std::move(panel), open}); return true;
}
void PanelRegistry::Draw(const EditorContext& context) {
    for(auto& entry : entries_) if(entry.isOpen) {
        const auto title = context.text.Label(entry.panel->TitleKey(), entry.panel->Id());
        if(ImGui::Begin(title.c_str(), &entry.isOpen)) entry.panel->Draw(context);
        ImGui::End();
    }
}
void PanelRegistry::Open(std::string_view id) { for(auto& entry : entries_) if(entry.panel->Id() == id) entry.isOpen = true; }
}
