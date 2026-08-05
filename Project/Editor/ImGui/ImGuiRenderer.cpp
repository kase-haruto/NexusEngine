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
	}

	Result<void> ImGuiRenderer::Initialize(const GraphicsRenderExtensionContext& context) {
		if(initialized_ || context.device == nullptr || context.commandQueue == nullptr ||
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
		ImGui::StyleColorsDark();

		descriptors_ = context.resourceDescriptors;
		if(!ImGui_ImplWin32_Init(static_cast<HWND>(context.nativeWindow))) {
			Shutdown();
			return std::unexpected(Error(ErrorCategory::Graphics, kBackendInitializationFailed, "ImGui Win32 backend initialization failed."));
		}

		ImGui_ImplDX12_InitInfo info;
		info.Device = context.device;
		info.CommandQueue = context.commandQueue;
		info.NumFramesInFlight = static_cast<int>(context.framesInFlight);
		info.RTVFormat = context.renderTargetFormat;
		info.DSVFormat = DXGI_FORMAT_UNKNOWN;
		info.UserData = this;
		info.SrvDescriptorHeap = descriptors_->GetHeap();
		info.SrvDescriptorAllocFn = &ImGuiRenderer::AllocateDescriptor;
		info.SrvDescriptorFreeFn = &ImGuiRenderer::FreeDescriptor;
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
		// 中央Dock領域ではMain RenderTargetをそのまま表示し、Editor UI背景でゲーム描画を覆わない。
		ImGui::DockSpaceOverViewport(
			0,
			ImGui::GetMainViewport(),
			ImGuiDockNodeFlags_PassthruCentralNode);
		ImGui::ShowDemoWindow();
	}

	void ImGuiRenderer::Record(ID3D12GraphicsCommandList* const commandList) {
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
} // namespace NexusEngine
