#include "ImGuiStyle.h"
#include "ThirdParty/imgui/imgui.h"
#include <algorithm>
#include <cmath>

namespace NexusEngine::UI {
namespace {
		namespace EditorStyleMetrics {
			const ImVec2 kWindowPadding { 8.0f, 8.0f };
			const ImVec2 kFramePadding { 6.0f, 3.0f };
			const ImVec2 kCellPadding { 4.0f, 4.0f };
			const ImVec2 kItemSpacing { 8.0f, 4.0f };
			const ImVec2 kItemInnerSpacing { 4.0f, 4.0f };
			const ImVec2 kNoExtraPadding { 0.0f, 0.0f };
			constexpr float kIndentSpacing = 21.0f;
			constexpr float kScrollbarSize = 12.0f;
			constexpr float kGrabMinSize = 10.0f;
			constexpr float kNoBorder = 0.0f;
			constexpr float kPopupBorder = 1.0f;
			constexpr float kSquareCorner = 0.0f;
			constexpr float kControlRounding = 4.0f;
			constexpr float kScrollbarRounding = 9.0f;
			constexpr float kTabRounding = 4.0f;
			constexpr float kLogSliderDeadzone = 4.0f;
			constexpr float kDockingSeparatorSize = 4.0f;
		}

		namespace EditorStylePalette {
			const ImVec4 kTransparent { 0.00f, 0.00f, 0.00f, 0.00f };
			const ImVec4 kBackground { 0.145f, 0.145f, 0.145f, 1.00f };
			const ImVec4 kPanel { 0.115f, 0.115f, 0.115f, 1.00f };
			const ImVec4 kPanelRaised { 0.185f, 0.185f, 0.185f, 1.00f };
			const ImVec4 kPanelHovered { 0.240f, 0.240f, 0.240f, 1.00f };
			const ImVec4 kPanelActive { 0.290f, 0.290f, 0.290f, 1.00f };
			const ImVec4 kInputBackground { 0.205f, 0.205f, 0.205f, 1.00f };
			const ImVec4 kInputHovered { 0.260f, 0.260f, 0.260f, 1.00f };
			const ImVec4 kInputActive { 0.310f, 0.310f, 0.310f, 1.00f };
			const ImVec4 kBorder { 0.080f, 0.080f, 0.080f, 1.00f };
			const ImVec4 kText { 0.900f, 0.900f, 0.900f, 1.00f };
			const ImVec4 kTextDisabled { 0.550f, 0.550f, 0.550f, 1.00f };
			const ImVec4 kAccent { 0.950f, 0.490f, 0.180f, 1.00f };
			const ImVec4 kAccentHovered { 1.000f, 0.590f, 0.280f, 1.00f };
			const ImVec4 kAccentActive { 0.800f, 0.370f, 0.100f, 1.00f };
			const ImVec4 kAccentSubtle { 0.340f, 0.235f, 0.165f, 1.00f };
			const ImVec4 kAccentSelection { 0.950f, 0.490f, 0.180f, 0.30f };
			const ImVec4 kAccentDockingPreview { 0.950f, 0.490f, 0.180f, 0.55f };
			const ImVec4 kScrollbarBackground { 0.020f, 0.020f, 0.020f, 0.53f };
			const ImVec4 kScrollbar { 0.310f, 0.310f, 0.310f, 1.00f };
			const ImVec4 kScrollbarHovered { 0.410f, 0.410f, 0.410f, 1.00f };
			const ImVec4 kScrollbarActive { 0.510f, 0.510f, 0.510f, 1.00f };
			const ImVec4 kTableRowAlternate { 0.160f, 0.160f, 0.160f, 0.35f };
			const ImVec4 kNavigationHighlight { 1.000f, 1.000f, 1.000f, 0.70f };
			const ImVec4 kNavigationDim { 0.000f, 0.000f, 0.000f, 0.35f };
			const ImVec4 kModalDim { 0.000f, 0.000f, 0.000f, 0.72f };
		}

		/////////////////////////////////////////////////////////////////////////////////////////
		// NexusEngine Editorで共通利用する黒基調とOrange AccentのStyleを構築する
		/////////////////////////////////////////////////////////////////////////////////////////
		void ApplyBaseEditorStyle() noexcept {
			ImGuiStyle& style = ImGui::GetStyle();

			using namespace EditorStyleMetrics;
			style.WindowPadding = kWindowPadding;
			style.FramePadding = kFramePadding;
			style.CellPadding = kCellPadding;
			style.ItemSpacing = kItemSpacing;
			style.ItemInnerSpacing = kItemInnerSpacing;
			style.TouchExtraPadding = kNoExtraPadding;
			style.IndentSpacing = kIndentSpacing;
			style.ScrollbarSize = kScrollbarSize;
			style.GrabMinSize = kGrabMinSize;
			style.WindowBorderSize = kNoBorder;
			style.ChildBorderSize = kNoBorder;
			style.PopupBorderSize = kPopupBorder;
			style.FrameBorderSize = kNoBorder;
			style.TabBorderSize = kNoBorder;
			style.WindowRounding = kSquareCorner;
			style.ChildRounding = kSquareCorner;
			style.FrameRounding = kControlRounding;
			style.PopupRounding = kControlRounding;
			style.ScrollbarRounding = kScrollbarRounding;
			style.GrabRounding = kControlRounding;
			style.TabRounding = kTabRounding;
			style.LogSliderDeadzone = kLogSliderDeadzone;
			style.DockingSeparatorSize = kDockingSeparatorSize;
			style.WindowMenuButtonPosition = ImGuiDir_Right;
			style.TabBarBorderSize = 1.0f;
			style.TabBarOverlineSize = 2.0f;

			using namespace EditorStylePalette;
			ImVec4* colors = style.Colors;
			colors[ImGuiCol_Text] = kText;
			colors[ImGuiCol_TextDisabled] = kTextDisabled;
			colors[ImGuiCol_WindowBg] = kBackground;
			colors[ImGuiCol_ChildBg] = kBackground;
			colors[ImGuiCol_PopupBg] = kPanel;
			colors[ImGuiCol_Border] = kBorder;
			colors[ImGuiCol_BorderShadow] = kTransparent;
			colors[ImGuiCol_FrameBg] = kInputBackground;
			colors[ImGuiCol_FrameBgHovered] = kInputHovered;
			colors[ImGuiCol_FrameBgActive] = kInputActive;
			colors[ImGuiCol_TitleBg] = kPanel;
			colors[ImGuiCol_TitleBgActive] = kPanelRaised;
			colors[ImGuiCol_TitleBgCollapsed] = kPanel;
			colors[ImGuiCol_MenuBarBg] = kPanel;
			colors[ImGuiCol_ScrollbarBg] = kScrollbarBackground;
			colors[ImGuiCol_ScrollbarGrab] = kScrollbar;
			colors[ImGuiCol_ScrollbarGrabHovered] = kScrollbarHovered;
			colors[ImGuiCol_ScrollbarGrabActive] = kScrollbarActive;
			colors[ImGuiCol_CheckMark] = kAccent;
			colors[ImGuiCol_SliderGrab] = kAccent;
			colors[ImGuiCol_SliderGrabActive] = kAccentActive;
			colors[ImGuiCol_Button] = kPanel;
			colors[ImGuiCol_ButtonHovered] = kPanelHovered;
			colors[ImGuiCol_ButtonActive] = kPanelActive;
			colors[ImGuiCol_Header] = kPanelRaised;
			colors[ImGuiCol_HeaderHovered] = kPanelHovered;
			colors[ImGuiCol_HeaderActive] = kAccentSubtle;
			colors[ImGuiCol_Separator] = kBorder;
			colors[ImGuiCol_SeparatorHovered] = kAccentHovered;
			colors[ImGuiCol_SeparatorActive] = kAccentActive;
			colors[ImGuiCol_ResizeGrip] = kAccentSubtle;
			colors[ImGuiCol_ResizeGripHovered] = kAccentHovered;
			colors[ImGuiCol_ResizeGripActive] = kAccentActive;
			colors[ImGuiCol_Tab] = kPanel;
			colors[ImGuiCol_TabHovered] = kPanelHovered;
			colors[ImGuiCol_TabSelected] = kBackground;
			colors[ImGuiCol_TabSelectedOverline] = kAccent;
			colors[ImGuiCol_TabDimmed] = kBackground;
			colors[ImGuiCol_TabDimmedSelected] = kPanel;
			colors[ImGuiCol_TabDimmedSelectedOverline] = kPanelActive;
			colors[ImGuiCol_DockingPreview] = kAccentDockingPreview;
			colors[ImGuiCol_DockingEmptyBg] = kPanelRaised;
			colors[ImGuiCol_TableHeaderBg] = kPanelRaised;
			colors[ImGuiCol_TableBorderStrong] = kBorder;
			colors[ImGuiCol_TableBorderLight] = kPanelHovered;
			colors[ImGuiCol_TableRowBg] = kTransparent;
			colors[ImGuiCol_TableRowBgAlt] = kTableRowAlternate;
			colors[ImGuiCol_TextSelectedBg] = kAccentSelection;
			colors[ImGuiCol_DragDropTarget] = kAccent;
			colors[ImGuiCol_NavCursor] = kAccent;
			colors[ImGuiCol_NavWindowingHighlight] = kNavigationHighlight;
			colors[ImGuiCol_NavWindowingDimBg] = kNavigationDim;
			colors[ImGuiCol_ModalWindowDimBg] = kModalDim;
		}
}

void ApplyEditorStyle(const EditorStyleOptions& options) noexcept {
    // 基準寸法から毎回作り直し、設定変更を繰り返してもScaleAllSizesを累積させない。
    ImGui::GetStyle() = ImGuiStyle{};
    ApplyBaseEditorStyle();
    auto& style = ImGui::GetStyle();
    if(options.light) {
        // ユーザーのOrange Accentは維持し、無彩色だけを明色へ派生させる。
        for(int index = 0; index < ImGuiCol_COUNT; ++index) {
            if(index == ImGuiCol_ModalWindowDimBg || index == ImGuiCol_NavWindowingDimBg) continue;
            auto& color = style.Colors[index];
            if(color.w > 0 && color.x == color.y && color.y == color.z)
                color.x = color.y = color.z = 1.0f - color.x;
        }
    }
    const float scale = std::isfinite(options.scale) ? std::clamp(options.scale, 0.5f, 12.0f) : 1.0f;
    style.FontSizeBase = std::isfinite(options.fontSize) ? std::clamp(options.fontSize, 10.0f, 40.0f) : 18.0f;
    style.FontScaleMain = scale;
    style.ScaleAllSizes(scale);
}
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

