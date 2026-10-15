#include "ImGuiStyle.h"
#include "ThirdParty/imgui/imgui.h"
#include <algorithm>
#include <cmath>

namespace NexusEngine::UI {
namespace {
		namespace EditorStyleMetrics {
			const ImVec2 kWindowPadding { 8.0f, 8.0f };
			const ImVec2 kFramePadding { 5.0f, 5.0f };
			const ImVec2 kCellPadding { 4.0f, 4.0f };
			const ImVec2 kItemSpacing { 8.0f, 4.0f };
			const ImVec2 kItemInnerSpacing { 4.0f, 4.0f };
			const ImVec2 kNoExtraPadding { 0.0f, 0.0f };
			constexpr float kIndentSpacing = 21.0f;
			constexpr float kScrollbarSize = 14.0f;
			constexpr float kGrabMinSize = 10.0f;
			constexpr float kNoBorder = 0.0f;
			constexpr float kPopupBorder = 1.0f;
			constexpr float kSquareCorner = 0.0f;
			constexpr float kControlRounding = 3.0f;
			constexpr float kScrollbarRounding = 9.0f;
			constexpr float kTabRounding = 4.0f;
			constexpr float kLogSliderDeadzone = 4.0f;
			constexpr float kDockingSeparatorSize = 4.0f;
		}

		namespace EditorStylePalette {
			const ImVec4 kTransparent { 0.00f, 0.00f, 0.00f, 0.00f };
			const ImVec4 kBackground { 0.055f, 0.055f, 0.055f, 1.00f };
			const ImVec4 kPanel { 0.090f, 0.090f, 0.090f, 1.00f };
			const ImVec4 kPanelRaised { 0.125f, 0.125f, 0.125f, 1.00f };
			const ImVec4 kPanelHovered { 0.180f, 0.180f, 0.180f, 1.00f };
			const ImVec4 kPanelActive { 0.245f, 0.245f, 0.245f, 1.00f };
			const ImVec4 kInputBackground { 0.030f, 0.030f, 0.030f, 1.00f };
			const ImVec4 kInputHovered { 0.095f, 0.095f, 0.095f, 1.00f };
			const ImVec4 kInputActive { 0.125f, 0.125f, 0.125f, 1.00f };
			const ImVec4 kBorder { 0.025f, 0.025f, 0.025f, 1.00f };
			const ImVec4 kText { 0.900f, 0.900f, 0.900f, 1.00f };
			const ImVec4 kTextDisabled { 0.550f, 0.550f, 0.550f, 1.00f };
			const ImVec4 kAccent { 1.000f, 0.350f, 0.100f, 1.00f };
			const ImVec4 kAccentHovered { 1.000f, 0.470f, 0.180f, 1.00f };
			const ImVec4 kAccentActive { 0.820f, 0.240f, 0.030f, 1.00f };
			const ImVec4 kAccentSubtle { 0.420f, 0.160f, 0.045f, 1.00f };
			const ImVec4 kAccentSelection { 1.000f, 0.350f, 0.100f, 0.35f };
			const ImVec4 kAccentDockingPreview { 1.000f, 0.350f, 0.100f, 0.70f };
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
			colors[ImGuiCol_TitleBg] = kBackground;
			colors[ImGuiCol_TitleBgActive] = kBackground;
			colors[ImGuiCol_TitleBgCollapsed] = kBackground;
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
			colors[ImGuiCol_Separator] = kBackground;
			colors[ImGuiCol_SeparatorHovered] = kAccentHovered;
			colors[ImGuiCol_SeparatorActive] = kAccentActive;
			colors[ImGuiCol_ResizeGrip] = kAccentSubtle;
			colors[ImGuiCol_ResizeGripHovered] = kAccentHovered;
			colors[ImGuiCol_ResizeGripActive] = kAccentActive;
			colors[ImGuiCol_Tab] = kBackground;
			colors[ImGuiCol_TabHovered] = kPanelHovered;
			colors[ImGuiCol_TabSelected] = kPanel;
			colors[ImGuiCol_TabSelectedOverline] = kAccent;
			colors[ImGuiCol_TabDimmed] = kBackground;
			colors[ImGuiCol_TabDimmedSelected] = kPanel;
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
    const float scale = std::isfinite(options.scale) ? std::clamp(options.scale, 0.5f, 3.0f) : 1.0f;
    style.FontSizeBase = std::isfinite(options.fontSize) ? std::clamp(options.fontSize, 10.0f, 40.0f) : 18.0f;
    style.FontScaleMain = scale;
    style.ScaleAllSizes(scale);
}
}

