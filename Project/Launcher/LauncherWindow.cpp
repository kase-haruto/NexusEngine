#include "LauncherWindow.h"
#include "Tools/UI/ImGuiStyle.h"
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include <fstream>
#include <limits>
#include <wincodec.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace NexusEngine {
	namespace {
		Error Failed(const char* message, HRESULT result = E_FAIL) { return Error(ErrorCategory::Window, 201, message, result); }
	} // namespace
	LauncherWindow::~LauncherWindow() {
		window_.SetMessageHandler(nullptr, nullptr);
		if(renderer_) ImGui_ImplDX11_Shutdown();
		if(win32_) ImGui_ImplWin32_Shutdown();
		if(imgui_) ImGui::DestroyContext();
		if(context_) {
			context_->ClearState();
			context_->Flush();
		}
	}
	bool LauncherWindow::Message(void*, HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
		if(message == WM_GETMINMAXINFO) {
			const UINT dpi = GetDpiForWindow(window);
			RECT	   minimum{0, 0, MulDiv(600, static_cast<int>(dpi), 96), MulDiv(420, static_cast<int>(dpi), 96)};
			if(AdjustWindowRectExForDpi(&minimum, static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE)), FALSE,
										static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE)), dpi)) {
				auto* limits		   = reinterpret_cast<MINMAXINFO*>(lParam);
				limits->ptMinTrackSize = {minimum.right - minimum.left, minimum.bottom - minimum.top};
				return true;
			}
		}
		// 非表示検証の合成入力を、実デスクトップのcursorから届く離脱通知で無効化しない。
		if(message == WM_MOUSELEAVE && !IsWindowVisible(window)) return true;
		return ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam) != 0;
	}
	Result<void> LauncherWindow::CreateTarget() {
		Microsoft::WRL::ComPtr<ID3D11Texture2D> buffer;
		HRESULT									result = swapChain_->GetBuffer(0, IID_PPV_ARGS(&buffer));
		if(SUCCEEDED(result)) result = device_->CreateRenderTargetView(buffer.Get(), nullptr, &target_);
		if(FAILED(result)) return std::unexpected(Failed("Cannot create Launcher render target.", result));
		return {};
	}
	Result<void> LauncherWindow::Initialize(const std::filesystem::path& dataRoot, bool visible) {
		WindowDetail details;
		details.title		 = L"NexusEngine Launcher";
		details.clientWidth	 = 1120;
		details.clientHeight = 760;
		details.visible		 = visible;
		auto initialized	 = window_.Initialize(details);
		if(!initialized) return initialized;
		DXGI_SWAP_CHAIN_DESC description{};
		description.BufferCount		  = 2;
		description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		description.BufferUsage		  = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		description.OutputWindow	  = Handle();
		description.SampleDesc.Count  = 1;
		description.Windowed		  = TRUE;
		description.SwapEffect		  = DXGI_SWAP_EFFECT_DISCARD;
		D3D_FEATURE_LEVEL level{};
		auto			  result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
																 D3D11_SDK_VERSION, &description, &swapChain_, &device_, &level, &context_);
		// 自動検証やGPUを持たない環境でもUIだけはWARPで起動できる。
		if(FAILED(result)) result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
																  D3D11_SDK_VERSION, &description, &swapChain_, &device_, &level, &context_);
		if(FAILED(result)) return std::unexpected(Failed("Cannot create Launcher UI device.", result));
		auto target = CreateTarget();
		if(!target) return target;
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		imgui_	 = true;
		auto& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.IniFilename = nullptr;
		UI::ApplyStyle();
		// narrow filenameを経由せず、Unicode配置から読み込む。Atlasより先にメモリを破棄しない。
		for(const auto* filename : {L"inter.ttf", L"NotoSerifJP.ttf"}) {
			const auto	  file = dataRoot / "Resources/Assets/Fonts" / filename;
			std::ifstream input(file, std::ios::binary | std::ios::ate);
			if(!input) continue;
			const auto bytes = input.tellg();
			if(bytes <= 0 || bytes > 64 * 1024 * 1024) continue;
			fonts_.emplace_back(static_cast<size_t>(bytes));
			input.seekg(0);
			if(!input.read(fonts_.back().data(), bytes)) {
				fonts_.pop_back();
				continue;
			}
			ImFontConfig config;
			config.FontDataOwnedByAtlas = false;
			config.MergeMode			= io.Fonts->Fonts.Size > 0;
			io.Fonts->AddFontFromMemoryTTF(fonts_.back().data(), static_cast<int>(bytes), 17, &config);
		}
		if(io.Fonts->Fonts.empty()) io.Fonts->AddFontDefault();
		win32_ = ImGui_ImplWin32_Init(Handle());
		if(!win32_) return std::unexpected(Failed("Cannot initialize Launcher input."));
		renderer_ = ImGui_ImplDX11_Init(device_.Get(), context_.Get());
		if(!renderer_) return std::unexpected(Failed("Cannot initialize Launcher renderer."));
		window_.SetMessageHandler(&Message, this);
		return {};
	}
	Result<bool> LauncherWindow::Frame(const std::function<void()>& draw, const std::filesystem::path& capture) {
		if(!window_.ProcessEvents()) return false;
		if(const auto resize = window_.ConsumeResizeEvent()) {
			context_->OMSetRenderTargets(0, nullptr, nullptr);
			target_.Reset();
			auto result = swapChain_->ResizeBuffers(0, resize->width, resize->height, DXGI_FORMAT_UNKNOWN, 0);
			if(FAILED(result)) return std::unexpected(Failed("Launcher resize failed.", result));
			auto created = CreateTarget();
			if(!created) return std::unexpected(created.error());
		}
		if(IsIconic(Handle())) {
			Sleep(16);
			return true;
		}
		const float scale = static_cast<float>(GetDpiForWindow(Handle())) / 96.0f;
		if(scale != dpi_) {
			ImGui::GetStyle().ScaleAllSizes(scale / dpi_);
			dpi_ = scale;
		}
		ImGui::GetStyle().FontScaleDpi = dpi_;
		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		draw();
		ImGui::Render();
		const auto	background = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
		const float clear[4]{background.x, background.y, background.z, background.w};
		auto*		view = target_.Get();
		context_->OMSetRenderTargets(1, &view, nullptr);
		context_->ClearRenderTargetView(view, clear);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		if(!capture.empty()) {
			auto saved = Capture(capture);
			if(!saved) return std::unexpected(saved.error());
		}
		const auto presented = swapChain_->Present(1, 0);
		if(FAILED(presented)) return std::unexpected(Failed("Launcher presentation failed.", presented));
		if(presented == DXGI_STATUS_OCCLUDED || !IsWindowVisible(Handle())) Sleep(16);
		return true;
	}
	Result<void> LauncherWindow::Capture(const std::filesystem::path& destination) {
		Microsoft::WRL::ComPtr<ID3D11Texture2D> buffer, staging;
		HRESULT									result = swapChain_->GetBuffer(0, IID_PPV_ARGS(&buffer));
		if(FAILED(result)) return std::unexpected(Failed("Cannot read Launcher frame.", result));
		D3D11_TEXTURE2D_DESC description{};
		buffer->GetDesc(&description);
		description.Usage		   = D3D11_USAGE_STAGING;
		description.BindFlags	   = 0;
		description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		description.MiscFlags	   = 0;
		result					   = device_->CreateTexture2D(&description, nullptr, &staging);
		if(FAILED(result)) return std::unexpected(Failed("Cannot stage Launcher frame.", result));
		context_->CopyResource(staging.Get(), buffer.Get());
		D3D11_MAPPED_SUBRESOURCE mapped{};
		result = context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
		if(FAILED(result)) return std::unexpected(Failed("Cannot map Launcher frame.", result));
		// Mapの寿命中だけWICへ渡し、部分失敗でも必ずUnmapする。
		struct Unmap {
			ID3D11DeviceContext* context;
			ID3D11Resource*		 resource;
			~Unmap() { context->Unmap(resource, 0); }
		} unmap{context_.Get(), staging.Get()};
		if(static_cast<uint64_t>(mapped.RowPitch) * description.Height > std::numeric_limits<UINT>::max()) return std::unexpected(Failed("Launcher frame exceeds capture limit."));
		Microsoft::WRL::ComPtr<IWICImagingFactory>	  factory;
		Microsoft::WRL::ComPtr<IWICStream>			  stream;
		Microsoft::WRL::ComPtr<IWICBitmapEncoder>	  encoder;
		Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
		Microsoft::WRL::ComPtr<IWICBitmap>			  bitmap;
		Microsoft::WRL::ComPtr<IWICFormatConverter>	  converter;
		result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
		if(SUCCEEDED(result)) result = factory->CreateStream(&stream);
		if(SUCCEEDED(result)) result = stream->InitializeFromFilename(destination.c_str(), GENERIC_WRITE);
		if(SUCCEEDED(result)) result = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
		if(SUCCEEDED(result)) result = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
		if(SUCCEEDED(result)) result = encoder->CreateNewFrame(&frame, nullptr);
		if(SUCCEEDED(result)) result = frame->Initialize(nullptr);
		if(SUCCEEDED(result)) result = frame->SetSize(description.Width, description.Height);
		WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
		if(SUCCEEDED(result)) result = frame->SetPixelFormat(&format);
		// SwapChainのRGBAをencoderが実際に受理した形式へ変換する。
		if(SUCCEEDED(result)) result = factory->CreateBitmapFromMemory(description.Width, description.Height, GUID_WICPixelFormat32bppRGBA,
																	   mapped.RowPitch, mapped.RowPitch * description.Height, static_cast<BYTE*>(mapped.pData), &bitmap);
		if(SUCCEEDED(result)) result = factory->CreateFormatConverter(&converter);
		if(SUCCEEDED(result)) result = converter->Initialize(bitmap.Get(), format, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);
		if(SUCCEEDED(result)) result = frame->WriteSource(converter.Get(), nullptr);
		if(SUCCEEDED(result)) result = frame->Commit();
		if(SUCCEEDED(result)) result = encoder->Commit();
		if(FAILED(result)) return std::unexpected(Failed("Cannot save Launcher capture.", result));
		return {};
	}
} // namespace NexusEngine
