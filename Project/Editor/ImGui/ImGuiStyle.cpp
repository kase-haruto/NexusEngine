#include "ImGuiStyle.h"
#include "ThirdParty/imgui/imgui.h"
namespace NexusEngine::UI {
void ApplyEditorStyle(const EditorStyleOptions& options) noexcept { ApplyStyle(options); }
Theme CurrentEditorTheme() noexcept {
    const auto& style = ImGui::GetStyle();
    const auto color = [&](ImGuiCol index) { const auto& c = style.Colors[index]; return Color{c.x,c.y,c.z,c.w}; };
    Theme result;
    result.text = color(ImGuiCol_Text); result.textDisabled = color(ImGuiCol_TextDisabled);
    result.surface = color(ImGuiCol_FrameBg); result.surfaceHovered = color(ImGuiCol_FrameBgHovered);
    result.surfaceActive = color(ImGuiCol_FrameBgActive); result.accent = color(ImGuiCol_CheckMark);
    result.itemSpacing = {style.ItemSpacing.x,style.ItemSpacing.y};
    result.framePadding = {style.FramePadding.x,style.FramePadding.y};
    result.controlRounding = style.FrameRounding; result.disabledOpacity = style.DisabledAlpha;
    return result;
}
}

