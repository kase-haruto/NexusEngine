#include "PanelRegistry.h"
#include "Editor/Services/LocalizationService.h"
#include "ThirdParty/imgui/imgui.h"
#include <algorithm>

namespace NexusEngine {
bool PanelRegistry::Register(std::unique_ptr<IEditorPanel> panel, bool open) {
    if(drawing_ || !panel || panel->Id().empty()) return false;
    for(const auto& entry : entries_) if(entry.Id() == panel->Id()) return false;
    auto id = std::string(panel->Id()), title = std::string(panel->TitleKey());
    entries_.push_back({std::move(panel), open, std::move(id), std::move(title), {}}); return true;
}

bool PanelRegistry::RegisterFactory(std::string id, std::string titleKey, std::function<std::unique_ptr<IEditorPanel>()> factory, bool open) {
    if(drawing_ || id.empty() || !factory) return false;
    for(const auto& entry : entries_) if(entry.Id() == id) return false;
    entries_.push_back({{}, open, std::move(id), std::move(titleKey), std::move(factory)}); return true;
}
bool PanelRegistry::Unregister(std::string_view id) {
    for(const auto& entry : entries_) if(entry.Id() == id) {
        if(drawing_) removed_.emplace_back(id);
        else std::erase_if(entries_, [&](const auto& item) { return item.Id() == id; });
        return true;
    }
    return false;
}
void PanelRegistry::Clear() {
    if(drawing_) { for(const auto& entry : entries_) removed_.push_back(entry.id); }
    else { entries_.clear(); removed_.clear(); }
}
void PanelRegistry::Draw(const EditorContext& context, bool disableEditing) {
    drawing_ = true;
    for(auto& entry : entries_) if(entry.isOpen) {
        if(!entry.panel && entry.factory) entry.panel = entry.factory();
        if(!entry.panel || entry.panel->Id() != entry.Id()) { entry.panel.reset(); entry.isOpen = false; continue; }
        const auto title = context.text.Label(entry.TitleKey(), entry.Id());
        // 保存済み配置にない新規Panelも、Empty Stateの折返しで極小幅にならないようにする。
        // FirstUseEverなので既存のDock配置や利用者が変更したサイズは維持する。
        ImGui::SetNextWindowSize({360, 480}, ImGuiCond_FirstUseEver);
        // パネルが宣言したスクロール方針を、Windowを開く前にフラグへ反映する。
        // ホイールのパネルスクロールだけを止めるため、Viewport側のカメラ操作は継続できる。
        ImGuiWindowFlags flags = ImGuiWindowFlags_None;
        if(!entry.panel->IsScrollable()) {
            flags |= ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        }
        if(ImGui::Begin(title.c_str(), &entry.isOpen, flags)) {
            // DisabledAlpha also dims Image vertices. Keep the runtime viewport at full
            // brightness while preventing edits in the other panels during Play.
            ImGui::BeginDisabled(disableEditing && entry.Id() != "Viewport");
            entry.panel->Draw(context);
            ImGui::EndDisabled();
        }
        ImGui::End();
    }
    drawing_ = false;
    for(const auto& id : removed_) Unregister(id);
    removed_.clear();
}
void PanelRegistry::Open(std::string_view id) { for(auto& entry : entries_) if(entry.Id() == id) entry.isOpen = true; }
}
