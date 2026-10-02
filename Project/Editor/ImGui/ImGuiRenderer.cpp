#include "ImGuiRenderer.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include "Foundation/Logging/Logger.h"
#include "Graphics/Descriptor/DescriptorAllocator.h"
#include "Graphics/Renderer/GraphicsContext.h"
#include "Graphics/Resource/TextureFormatDx12.h"
#include "ThirdParty/imgui/imgui.h"
#include "ThirdParty/imgui/backends/imgui_impl_dx12.h"
#include "ThirdParty/imgui/backends/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

namespace NexusEngine {
	namespace {
		constexpr int32_t kInvalidContext = 1;
		constexpr int32_t kBackendInitializationFailed = 2;
		constexpr int32_t kFontLoadFailed = 3;
		constexpr float kEditorFontSize = 18.0f;
		constexpr const char* kInterFontPath = "Resources/Assets/Fonts/inter.ttf";
		constexpr const char* kJapaneseFontPath = "Resources/Assets/Fonts/NotoSerifJP.ttf";

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
		void ApplyEditorStyle() noexcept {
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

	Result<void> ImGuiRenderer::Initialize(const GraphicsRenderExtensionContext& context) {
		if(initialized_ || context.nativeDevice == nullptr || context.nativeCommandQueue == nullptr ||
		   context.resourceDescriptors == nullptr || context.nativeWindow == nullptr) {
			return std::unexpected(Error(ErrorCategory::Graphics, kInvalidContext, "ImGui renderer context is invalid."));
		}

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

		// 欧文UIはInterを使用し、日本語グリフだけをNoto Serif JPから同じFontへ補完する。
		// Dear ImGui 1.92以降は必要なGlyphを動的にRasterizeするため、固定Glyph Rangeは指定しない。
		ImFont* const interFont = io.Fonts->AddFontFromFileTTF(kInterFontPath, kEditorFontSize);
		if(interFont == nullptr) {
			ImGui::DestroyContext();
			return std::unexpected(Error(ErrorCategory::Graphics, kFontLoadFailed, "Failed to load Resources/Assets/Fonts/inter.ttf."));
		}
		ImFontConfig japaneseFontConfig;
		japaneseFontConfig.MergeMode = true;
		japaneseFontConfig.PixelSnapH = true;
		if(io.Fonts->AddFontFromFileTTF(kJapaneseFontPath, kEditorFontSize, &japaneseFontConfig) == nullptr) {
			ImGui::DestroyContext();
			return std::unexpected(Error(ErrorCategory::Graphics, kFontLoadFailed, "Failed to load Resources/Assets/Fonts/NotoSerifJP.ttf."));
		}
		io.FontDefault = interFont;
		ApplyEditorStyle();

		descriptors_ = context.resourceDescriptors;
		if(!ImGui_ImplWin32_Init(static_cast<HWND>(context.nativeWindow))) {
			Shutdown();
			return std::unexpected(Error(ErrorCategory::Graphics, kBackendInitializationFailed, "ImGui Win32 backend initialization failed."));
		}

		ImGui_ImplDX12_InitInfo info;
		info.Device = static_cast<ID3D12Device*>(context.nativeDevice);
		info.CommandQueue = static_cast<ID3D12CommandQueue*>(context.nativeCommandQueue);
		info.NumFramesInFlight = static_cast<int>(context.framesInFlight);
		info.RTVFormat = ToNativeTextureFormat(context.renderTargetFormat);
		info.DSVFormat = DXGI_FORMAT_UNKNOWN;
		info.UserData = this;
		info.SrvDescriptorHeap = descriptors_->GetHeap();
		info.SrvDescriptorAllocFn = [](
			ImGui_ImplDX12_InitInfo* const backendInfo,
			D3D12_CPU_DESCRIPTOR_HANDLE* const cpu,
			D3D12_GPU_DESCRIPTOR_HANDLE* const gpu) {
			auto* self = static_cast<ImGuiRenderer*>(backendInfo->UserData);
			auto result = self->descriptors_->Allocate();
			if(!result) {
				*cpu = {};
				*gpu = {};
				NEXUS_LOG_ERROR("ImGui", "Shared CBV/SRV/UAV descriptor heap is exhausted.");
				return;
			}
			self->allocations_.push_back(*result);
			*cpu = result->cpuHandle;
			*gpu = result->gpuHandle;
		};
		info.SrvDescriptorFreeFn = [](
			ImGui_ImplDX12_InitInfo* const backendInfo,
			const D3D12_CPU_DESCRIPTOR_HANDLE cpu,
			const D3D12_GPU_DESCRIPTOR_HANDLE gpu) {
			// GPU参照中の可能性があるためslotは保持し、GPU idle後のShutdownで解放する。
			static_cast<void>(backendInfo);
			static_cast<void>(cpu);
			static_cast<void>(gpu);
		};
		if(!ImGui_ImplDX12_Init(&info)) {
			ImGui_ImplWin32_Shutdown();
			Shutdown();
			return std::unexpected(Error(ErrorCategory::Graphics, kBackendInitializationFailed, "ImGui DX12 backend initialization failed."));
		}
		initialized_ = true;
		return {};
	}

	void ImGuiRenderer::BeginFrame() {
		if(!initialized_) return;
		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
	}

	void ImGuiRenderer::Record(GraphicsContext& context) {
		auto* const commandList = static_cast<ID3D12GraphicsCommandList*>(context.GetNativeCommandList());
		if(!initialized_ || commandList == nullptr) return;
		ImGui::Render();
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
	}

	void ImGuiRenderer::Shutdown() noexcept {
		if(initialized_) {
			ImGui_ImplDX12_Shutdown();
			ImGui_ImplWin32_Shutdown();
			initialized_ = false;
		}
		if(ImGui::GetCurrentContext() != nullptr) ImGui::DestroyContext();
		// GraphicsSystemはShutdown前にGPU idleを保証するため、ここで安全に全slotを返せる。
		if(descriptors_ != nullptr) {
			for(const auto& allocation : allocations_) static_cast<void>(descriptors_->Free(allocation));
		}
		allocations_.clear();
		descriptors_ = nullptr;
	}

	bool ImGuiRenderer::HandleWindowMessage(void* const window, const uint32_t message, const uintptr_t wParam, const intptr_t lParam) noexcept {
		if(ImGui::GetCurrentContext() == nullptr) return false;
		return ImGui_ImplWin32_WndProcHandler(static_cast<HWND>(window), message, static_cast<WPARAM>(wParam), static_cast<LPARAM>(lParam)) != 0;
	}

#if 0
	void ImGuiRenderer::AllocateDescriptor(ImGui_ImplDX12_InitInfo* const info, D3D12_CPU_DESCRIPTOR_HANDLE* const cpu, D3D12_GPU_DESCRIPTOR_HANDLE* const gpu) {
		auto* self = static_cast<ImGuiRenderer*>(info->UserData);
		auto result = self->descriptors_->Allocate();
		if(!result) {
			*cpu = {};
			*gpu = {};
			NEXUS_LOG_ERROR("ImGui", "Shared CBV/SRV/UAV descriptor heap is exhausted.");
			return;
		}
		self->allocations_.push_back(*result);
		*cpu = result->cpuHandle;
		*gpu = result->gpuHandle;
	}

	void ImGuiRenderer::FreeDescriptor(ImGui_ImplDX12_InitInfo* const info, const D3D12_CPU_DESCRIPTOR_HANDLE cpu, const D3D12_GPU_DESCRIPTOR_HANDLE gpu) {
		auto* self = static_cast<ImGuiRenderer*>(info->UserData);
		// 実行中はGPU参照中の可能性があるためslotを保持し、GPU idle後のShutdownで一括解放する。
		// 現段階のDemo/Font用途では再生成頻度が低く、危険な即時再利用を避けることを優先する。
		static_cast<void>(cpu);
		static_cast<void>(gpu);
		static_cast<void>(self);
	}
#endif
} // namespace NexusEngine
