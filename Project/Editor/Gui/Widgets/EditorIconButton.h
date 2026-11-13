#pragma once
#include "ThirdParty/imgui/imgui.h"
#include <map>
#include <string>
#include <string_view>

namespace NexusEngine::UI {
// Textureの所有権はRenderer側にある。Context終了前にClearEditorIconsを呼ぶ。
inline std::map<ImGuiContext*, std::map<std::string, ImTextureRef>> editorIcons;
inline void SetEditorIcon(const std::string& key, ImTextureRef texture) {
    editorIcons[ImGui::GetCurrentContext()][key] = texture;
}
inline void ClearEditorIcons() { editorIcons.erase(ImGui::GetCurrentContext()); }
inline const ImTextureRef* FindEditorIcon(const std::string& key) {
    const auto context = editorIcons.find(ImGui::GetCurrentContext());
    if(context == editorIcons.end()) return nullptr;
    const auto found = context->second.find(key);
    return found == context->second.end() ? nullptr : &found->second;
}
// 装飾画像の後ろへ既存Widgetを置く。操作IDやDrag/Dropの対象は後ろのWidgetが維持する。
inline bool EditorInlineIcon(const std::string& key) {
    const auto* texture = FindEditorIcon(key);
    if(!texture) return false;
    const float side = ImGui::GetFontSize();
    ImGui::Image(*texture,{side,side});
    ImGui::SameLine(0,ImGui::GetStyle().ItemInnerSpacing.x);
    return true;
}
inline std::string EditorComponentIconKey(std::string_view type) {
    if(type == "Script" || type.starts_with("ManagedScript:")) return "component.Script";
    if(type == "RigidBody" || type == "Collider") return "component.Physics";
    return {};
}
// labelは従来の###IDも受け取れるため、画像の有無で操作IDが変わらない。
inline bool EditorIconButton(const std::string& key, const char* label, ImVec2 fallbackSize = {}) {
    if(const auto* texture = FindEditorIcon(key)) {
        const float side = ImGui::GetFontSize();
        const bool pressed = ImGui::ImageButton(label, *texture, {side, side});
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            auto title = std::string_view(label);
            title = title.substr(0, title.find("##"));
            ImGui::SetTooltip("%.*s", static_cast<int>(title.size()), title.data());
        }
        return pressed;
    }
    return ImGui::Button(label, fallbackSize);
}
}
