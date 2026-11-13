#pragma once
#include "ThirdParty/imgui/imgui.h"
#include "ThirdParty/imgui/imgui_internal.h"

namespace NexusEngine::EditorTitleBar {
inline constexpr const char* WindowName = "###EditorTitleBar";
inline ImGuiID Key(ImGuiWindow* window,const char* name) { return ImHashStr(name,0,window->ID); }
inline ImGuiWindow* Current() {
    auto* window = ImGui::FindWindowByName(WindowName);
    return window && window->LastFrameActive == ImGui::GetFrameCount() ? window : nullptr;
}
inline float Scale(ImGuiWindow* window) { return window->StateStorage.GetFloat(Key(window,"TitleScale"),1); }
inline void SetMenuBounds(ImGuiWindow* window,ImRect bounds) {
    window->StateStorage.SetFloat(Key(window,"MenuLeft"),bounds.Min.x);
    window->StateStorage.SetFloat(Key(window,"MenuTop"),bounds.Min.y);
    window->StateStorage.SetFloat(Key(window,"MenuRight"),bounds.Max.x);
    window->StateStorage.SetFloat(Key(window,"MenuBottom"),bounds.Max.y);
}
inline ImRect MenuBounds(ImGuiWindow* window) {
    return {{window->StateStorage.GetFloat(Key(window,"MenuLeft")),window->StateStorage.GetFloat(Key(window,"MenuTop"))},
        {window->StateStorage.GetFloat(Key(window,"MenuRight")),window->StateStorage.GetFloat(Key(window,"MenuBottom"))}};
}
}
