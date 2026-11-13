#include "ImGuiRenderer.h"
#include "ImGuiStyle.h"
#include "Editor/Gui/Widgets/EditorIconButton.h"
#include "Editor/Gui/EditorTitleBarLayout.h"

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
#include "Platform/Window/ProjectWindow.h"
#include "ThirdParty/imgui/imgui.h"
#include "ThirdParty/imgui/imgui_internal.h"
#include "ThirdParty/ImGuizmo/ImGuizmo.h"
#include <cstring>
#include <fstream>
#include <algorithm>
#include "ThirdParty/imgui/backends/imgui_impl_dx12.h"
#include "ThirdParty/imgui/backends/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

namespace NexusEngine {
    void ImGuiRenderer::LoadEditorIcons() {
        // engineRoot/Resources/Editor/Icons。Game側のAssetやthumbnail上限とは独立させる。
        if(iconDirectory_.empty()) return;
        std::error_code error;
        std::filesystem::directory_iterator files(iconDirectory_, error), end;
        for(; !error && files != end && icons_.size() < 128; files.increment(error)) {
            const auto path = files->path();
            if(path.extension() != ".png") continue;
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if(!input) continue;
            const auto size = input.tellg();
            if(size <= 0 || size > 4 * 1024 * 1024) continue;
            std::vector<uint8_t> bytes(static_cast<size_t>(size));
            input.seekg(0); input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if(!input) continue;
            auto image = WicImageDecoder{}.Decode(bytes);
            if(!image || !image->width || !image->height || image->width > 1024 || image->height > 1024) {
                NEXUS_LOG_WARNING("Editor", "Invalid editor icon: " + path.filename().string());
                continue;
            }
            auto texture = std::make_unique<ImTextureData>();
            texture->Create(ImTextureFormat_RGBA32, static_cast<int>(image->width), static_cast<int>(image->height));
            std::memcpy(texture->Pixels, image->rgbaPixels.data(), image->rgbaPixels.size());
            ImGui::RegisterUserTexture(texture.get());
            const auto key = path.stem().string();
            UI::SetEditorIcon(key, texture->GetTexRef());
            icons_.emplace(key, std::move(texture));
        }
    }
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
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

		// 欧文UIはInterを使用し、日本語グリフだけをNoto Serif JPから同じFontへ補完する。
		// Dear ImGui 1.92以降は必要なGlyphを動的にRasterizeするため、固定Glyph Rangeは指定しない。
		// Win32の狭い文字列fopenを通さず、Unicodeのfilesystem::pathで読む。
		fontData_.clear();
		const auto loadFont = [&](const wchar_t* name, ImFontConfig config) -> ImFont* {
			std::ifstream input(fontDirectory_ / name, std::ios::binary | std::ios::ate);
			if(!input) return nullptr;
			const auto size = input.tellg();
			if(size <= 0 || size > 64 * 1024 * 1024) return nullptr;
			fontData_.emplace_back(static_cast<size_t>(size));
			auto& bytes = fontData_.back();
			input.seekg(0); input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
			if(!input) return nullptr;
			config.FontDataOwnedByAtlas = false;
			return io.Fonts->AddFontFromMemoryTTF(bytes.data(), static_cast<int>(bytes.size()), kEditorFontSize, &config);
		};
		ImFont* const interFont = loadFont(L"inter.ttf", {});
		if(interFont == nullptr) {
			ImGui::DestroyContext();
			return std::unexpected(Error(ErrorCategory::Graphics, kFontLoadFailed, "Failed to load Resources/Assets/Fonts/inter.ttf."));
		}
		ImFontConfig japaneseFontConfig;
		japaneseFontConfig.MergeMode = true;
		japaneseFontConfig.PixelSnapH = true;
		if(loadFont(L"NotoSerifJP.ttf", japaneseFontConfig) == nullptr) {
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
		nativeWindow_ = context.nativeWindow;
		ProjectWindow::ConfigureCustomTitleBar(static_cast<HWND>(nativeWindow_),
			MulDiv(38, static_cast<int>(GetDpiForWindow(static_cast<HWND>(nativeWindow_))), 96),
			MulDiv(138, static_cast<int>(GetDpiForWindow(static_cast<HWND>(nativeWindow_))), 96));
		graphicsSystem_ = context.graphicsSystem;
		LoadEditorIcons();
		return {};
	}

	std::optional<UI::ResolvedTexture> ImGuiRenderer::Resolve(UI::TextureHandle texture) const noexcept {
		if(!initialized_ || !graphicsSystem_) return {};
		ViewportImage image;
		if(texture.value == SceneViewportTexture.value) image = graphicsSystem_->GetSceneViewportImage();
		else if(texture.value == CameraPreviewTexture.value) image = graphicsSystem_->GetCameraPreviewImage();
		else return {};
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
		DrawTitleBar();
	}

	void ImGuiRenderer::DrawTitleBar() {
		const auto window = static_cast<HWND>(nativeWindow_);
		if(!window) return;
		auto* viewport = ImGui::GetMainViewport();
		const float scale = static_cast<float>(GetDpiForWindow(window)) / 96.0f;
		const float height = std::max(38.0f * scale,ImGui::GetFrameHeight()+12*scale), buttonWidth = 46.0f * scale;
		ProjectWindow::ConfigureCustomTitleBar(window, static_cast<int>(height), static_cast<int>(buttonWidth * 3));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
		const auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoBringToFrontOnFocus;
		if(ImGui::BeginViewportSideBar("###EditorTitleBar", viewport, ImGuiDir_Up, height, flags)) {
            auto* title = ImGui::GetCurrentWindow();
            title->MenuBarHeight = height;
            title->StateStorage.SetFloat(EditorTitleBar::Key(title,"TitleScale"),scale);
            EditorTitleBar::SetMenuBounds(title,{});
			auto* draw = ImGui::GetWindowDrawList();
			const auto origin = ImGui::GetWindowPos();
			const float width = ImGui::GetWindowSize().x;
            // MenuBar追加後もロゴとWindow操作ボタンはタイトルバー全高で描画・Hit判定する。
            ImGui::PushClipRect(origin,{origin.x+width,origin.y+height},false);
			const bool focused = GetForegroundWindow() == window;
			const auto foreground = ImGui::GetColorU32(focused ? ImGuiCol_Text : ImGuiCol_TextDisabled);
			const auto accent = IM_COL32(242, 125, 46, focused ? 255 : 160);
			// Same NX paths and backing as Resources/Editor/Branding/NexusEngine.svg.
			const float logoSize = 28 * scale;
			const ImVec2 logo{origin.x + 12 * scale, origin.y + (height - logoSize) * .5f};
			const auto point = [&](float x, float y) { return ImVec2{logo.x + x * logoSize / 256, logo.y + y * logoSize / 256}; };
			draw->AddRectFilled(point(8, 8), point(248, 248), IM_COL32(35, 35, 35, 255), logoSize * 44 / 256);
			const ImVec2 n[]{point(42, 180), point(42, 76), point(114, 180), point(114, 76)};
			draw->AddPolyline(n, 4, accent, ImDrawFlags_None, logoSize * 13 / 256);
			draw->AddLine(point(146, 76), point(214, 180), accent, logoSize * 13 / 256);
			draw->AddLine(point(214, 76), point(146, 180), accent, logoSize * 13 / 256);
			const float controlsStart = origin.x + width - buttonWidth * 3;
			const bool maximized = IsZoomed(window) != FALSE;
			const bool resizable = (GetWindowLongPtrW(window, GWL_STYLE) & WS_MAXIMIZEBOX) != 0;
			const char* ids[]{"###Minimize", "###Maximize", "###Close"};
			const char* hints[]{"Minimize", maximized ? "Restore" : "Maximize", "Close"};
			for(int i = 0; i < 3; ++i) {
				const ImVec2 start{controlsStart + buttonWidth * i, origin.y};
				ImGui::SetCursorScreenPos(start);
				ImGui::BeginDisabled(i == 1 && !resizable);
				const bool clicked = ImGui::InvisibleButton(ids[i], {buttonWidth, height});
				const bool hovered = ImGui::IsItemHovered();
				if(hovered || ImGui::IsItemActive()) draw->AddRectFilled(start, {start.x + buttonWidth, start.y + height},
					i == 2 ? IM_COL32(196, 43, 52, 255) : ImGui::GetColorU32(ImGui::IsItemActive() ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered));
				const ImVec2 center{start.x + buttonWidth * .5f, start.y + height * .5f};
				const float r = 5 * scale, stroke = 1.2f * scale;
				const auto color = i == 2 && hovered ? IM_COL32_WHITE : (i == 1 && !resizable ? ImGui::GetColorU32(ImGuiCol_TextDisabled) : foreground);
				if(i == 0) draw->AddLine({center.x - r, center.y}, {center.x + r, center.y}, color, stroke);
				else if(i == 1) {
					if(maximized) {
						draw->AddRect({center.x - r + 3 * scale, center.y - r}, {center.x + r, center.y + r - 3 * scale}, color, 0, 0, stroke);
						draw->AddRectFilled({center.x - r, center.y - r + 3 * scale}, {center.x + r - 3 * scale, center.y + r}, ImGui::GetColorU32(hovered ? ImGuiCol_ButtonHovered : ImGuiCol_WindowBg));
						draw->AddRect({center.x - r, center.y - r + 3 * scale}, {center.x + r - 3 * scale, center.y + r}, color, 0, 0, stroke);
					} else draw->AddRect({center.x - r, center.y - r}, {center.x + r, center.y + r}, color, 0, 0, stroke);
				} else {
					draw->AddLine({center.x - r, center.y - r}, {center.x + r, center.y + r}, color, stroke);
					draw->AddLine({center.x - r, center.y + r}, {center.x + r, center.y - r}, color, stroke);
				}
				if(hovered) ImGui::SetTooltip("%s", hints[i]);
				ImGui::EndDisabled();
				// Queue the close request so the owner's WM_CLOSE/save-confirmation path runs next frame.
				if(clicked) PostMessageW(window, WM_SYSCOMMAND, i == 0 ? SC_MINIMIZE : i == 1 ? (maximized ? SC_RESTORE : SC_MAXIMIZE) : SC_CLOSE, 0);
			}
			draw->AddLine({origin.x, origin.y + height - 1}, {origin.x + width, origin.y + height - 1}, ImGui::GetColorU32(ImGuiCol_Border));
            ImGui::PopClipRect();
		}
		ImGui::End();
		ImGui::PopStyleColor();
		ImGui::PopStyleVar();
	}

	void ImGuiRenderer::DrawTitleBarCaption() {
        auto* title = EditorTitleBar::Current();
        if(!title || !nativeWindow_) return;
        const auto menu = EditorTitleBar::MenuBounds(title);
        const float scale = EditorTitleBar::Scale(title);
        const auto size = ImGui::CalcTextSize("NexusEngine");
        const float right = title->Pos.x+title->Size.x-138*scale;
        const float left = std::max(title->Pos.x+52*scale,menu.Max.x+16*scale);
        const float x = std::clamp(title->Pos.x+(title->Size.x-size.x)*.5f,left,std::max(left,right-size.x-8*scale));
        auto* draw = title->DrawList;
        if(right > left) {
            draw->PushClipRect({left,title->Pos.y},{right,title->Pos.y+title->Size.y},true);
            draw->AddText({x,title->Pos.y+(title->Size.y-size.y)*.5f},ImGui::GetColorU32(
                GetForegroundWindow() == static_cast<HWND>(nativeWindow_) ? ImGuiCol_Text : ImGuiCol_TextDisabled),"NexusEngine");
            draw->PopClipRect();
        }
        const RECT client{static_cast<LONG>(menu.Min.x-title->Pos.x),static_cast<LONG>(menu.Min.y-title->Pos.y),
            static_cast<LONG>(menu.Max.x-title->Pos.x+1),static_cast<LONG>(menu.Max.y-title->Pos.y+1)};
        ProjectWindow::ConfigureTitleBarClientArea(static_cast<HWND>(nativeWindow_),menu.GetWidth() > 0 ? client : RECT{});
	}

	void ImGuiRenderer::Record(GraphicsContext& context) {
		auto* const commandList = static_cast<ID3D12GraphicsCommandList*>(context.GetNativeCommandList());
		if(!initialized_ || commandList == nullptr) return;
		DrawTitleBarCaption();
		ImGui::Render();
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
	}

    void ImGuiRenderer::AfterSubmit() {
        if(!initialized_ || !(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)) return;
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }

	void ImGuiRenderer::Shutdown() noexcept {
		ProjectWindow::ConfigureCustomTitleBar(static_cast<HWND>(nativeWindow_), 0, 0);
		nativeWindow_ = nullptr;
		graphicsSystem_ = nullptr;
		if(initialized_) {
            for(auto& [key, texture] : icons_) {
                static_cast<void>(key);
                if(!ImGui::GetPlatformIO().Textures.contains(texture.get())) ImGui::GetPlatformIO().Textures.push_back(texture.get());
            }
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
            UI::ClearEditorIcons();
            for(auto& [key, texture] : icons_) { static_cast<void>(key); ImGui::UnregisterUserTexture(texture.get()); }
            icons_.clear();
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
