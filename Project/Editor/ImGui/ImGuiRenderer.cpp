#include "ImGuiRenderer.h"
#include "ImGuiStyle.h"

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
#include "Graphics/Renderer/GraphicsSystem.h"
#include "Graphics/Resource/TextureFormatDx12.h"
#include "Graphics/Resource/Depthstencil/DepthstencilFormatDx12.h"
#include "ThirdParty/imgui/imgui.h"
#include "ThirdParty/imgui/imgui_internal.h"
#include "ThirdParty/ImGuizmo/ImGuizmo.h"
#include <cstring>
#include "ThirdParty/imgui/backends/imgui_impl_dx12.h"
#include "ThirdParty/imgui/backends/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

namespace NexusEngine {
    ImTextureRef ImGuiRenderer::FindPreview(const std::string& key) const {
        auto found = previews_.find(key);
        return found == previews_.end() ? ImTextureRef{} : found->second->GetTexRef();
    }
    ImTextureRef ImGuiRenderer::CreatePreview(const std::string& key, const DecodedImage& image) {
        if(auto found = previews_.find(key); found != previews_.end()) return found->second->GetTexRef();
        if(!initialized_ || previews_.size() >= 256 || image.width != 160 || image.height != 160 || image.rgbaPixels.size() != 160*160*4) return {};
        auto texture = std::make_unique<ImTextureData>();
        texture->Create(ImTextureFormat_RGBA32, 160, 160);
        std::memcpy(texture->Pixels, image.rgbaPixels.data(), image.rgbaPixels.size());
        ImGui::RegisterUserTexture(texture.get());
        auto reference = texture->GetTexRef(); previews_.emplace(key, std::move(texture)); return reference;
    }
	namespace {
		constexpr int32_t kInvalidContext = 1;
		constexpr int32_t kBackendInitializationFailed = 2;
		constexpr int32_t kFontLoadFailed = 3;
		constexpr float kEditorFontSize = 18.0f;
		constexpr const char* kInterFontPath = "Resources/Assets/Fonts/inter.ttf";
		constexpr const char* kJapaneseFontPath = "Resources/Assets/Fonts/NotoSerifJP.ttf";

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
		UI::ApplyEditorStyle();

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
		// 呼出側のUI描画先に合わせる。Viewport EditorはDepthなしのBackBufferへUIを描く。
		info.DSVFormat = ToNativeDepthStencilFormat(context.depthStencilFormat);
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
		graphicsSystem_ = context.graphicsSystem;
		return {};
	}

	std::optional<UI::ResolvedTexture> ImGuiRenderer::Resolve(UI::TextureHandle texture) const noexcept {
		if(!initialized_ || !graphicsSystem_ || texture.value != SceneViewportTexture.value) return {};
		const auto image = graphicsSystem_->GetSceneViewportImage();
		if(!image.backendReference || !image.width || !image.height) return {};
		return UI::ResolvedTexture{image.backendReference, image.width, image.height};
	}

	void ImGuiRenderer::BeginFrame() {
		if(!initialized_) return;
		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
        ImGuizmo::SetImGuiContext(ImGui::GetCurrentContext());
        ImGuizmo::BeginFrame();
	}

	void ImGuiRenderer::Record(GraphicsContext& context) {
		auto* const commandList = static_cast<ID3D12GraphicsCommandList*>(context.GetNativeCommandList());
		if(!initialized_ || commandList == nullptr) return;
		ImGui::Render();
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
	}

	void ImGuiRenderer::Shutdown() noexcept {
		graphicsSystem_ = nullptr;
		if(initialized_) {
            // NewFrame前に作られた画像もBackendの停止対象へ含める。
            for(auto& [key, texture] : previews_) {
                static_cast<void>(key);
                if(!ImGui::GetPlatformIO().Textures.contains(texture.get())) ImGui::GetPlatformIO().Textures.push_back(texture.get());
            }
			ImGui_ImplDX12_Shutdown();
			ImGui_ImplWin32_Shutdown();
			initialized_ = false;
		}
        if(ImGui::GetCurrentContext() != nullptr) {
            for(auto& [key, texture] : previews_) { static_cast<void>(key); ImGui::UnregisterUserTexture(texture.get()); }
            previews_.clear();
            ImGui::DestroyContext();
        }
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
