#include "SwapChain.h"

// windows
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace NexusEngine {
	namespace {
		constexpr int32_t kInvalidArgument = 1;
		constexpr int32_t kSwapChainCreationFailed = 2;
		constexpr int32_t kRtvHeapCreationFailed = 3;
		constexpr int32_t kBackBufferAcquisitionFailed = 4;
		constexpr int32_t kPresentFailed = 5;
		constexpr int32_t kResizeFailed = 6;
	}

	SwapChain::~SwapChain() noexcept { Shutdown(); }

	/////////////////////////////////////////////////////////////////////////////////////////
	// 表示用SwapChain、RTV Heap、BackBufferを依存順に初期化する
	/////////////////////////////////////////////////////////////////////////////////////////
	Result<void> SwapChain::Initialize(
		IDXGIFactory4* const factory,
		ID3D12Device* const device,
		ID3D12CommandQueue* const queue,
		void* const nativeWindow,
		const uint32_t width,
		const uint32_t height,
		DescriptorAllocator* const rtvAllocator) {
		// DXGIへ不完全な値を渡す前に、Surface生成に必要な全依存と有効サイズを検証する。
		if(factory == nullptr || device == nullptr || queue == nullptr || nativeWindow == nullptr || width == 0 || height == 0 || rtvAllocator == nullptr) {
			return std::unexpected(Error(ErrorCategory::Graphics, kInvalidArgument, "SwapChain initialization arguments are invalid."));
		}

		// Flip Discardと2枚のBackBufferを採用し、従来Blit Modelを避ける。
		// MSAAはSwapChainへ直接適用せず、必要になった段階で中間RenderTargetとして扱う。
		DXGI_SWAP_CHAIN_DESC1 desc = {};
		desc.Width = width;
		desc.Height = height;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		desc.BufferCount = kBufferCount;
		desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

		// CreateSwapChainForHwndの戻り型から、BackBuffer index取得に必要なSwapChain3へ昇格する。
		Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain1;
		const auto window = static_cast<HWND>(nativeWindow);
		HRESULT result = factory->CreateSwapChainForHwnd(queue, window, &desc, nullptr, nullptr, &swapChain1);
		if(FAILED(result)) {
			Shutdown();
			return std::unexpected(MakeDirectXError(kSwapChainCreationFailed, result, "Failed to create swap chain."));
		}
		// Alt+EnterはWindow側の責務と競合するため、DXGIによる暗黙のFullscreen切替を無効化する。
		result = factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
		if(FAILED(result)) {
			Shutdown();
			return std::unexpected(MakeDirectXError(kSwapChainCreationFailed, result, "Failed to configure swap chain window association."));
		}
		result = swapChain1.As(&swapChain_);
		if(FAILED(result)) {
			Shutdown();
			return std::unexpected(MakeDirectXError(kSwapChainCreationFailed, result, "Failed to acquire IDXGISwapChain3."));
		}

		// Renderer共有RTV HeapからBackBuffer数分の連続領域を確保し、Resize後も同じ位置を再利用する。
		rtvAllocator_ = rtvAllocator;
		auto descriptorResult = rtvAllocator_->Allocate(kBufferCount);
		if(!descriptorResult) {
			Shutdown();
			return std::unexpected(std::move(descriptorResult.error()));
		}
		rtvDescriptors_ = *descriptorResult;
		width_ = width;
		height_ = height;
		return CreateRenderTargets(device);
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	// SwapChain所有物をBackBufferから逆順に解放する
	/////////////////////////////////////////////////////////////////////////////////////////
	void SwapChain::Shutdown() noexcept {
		// BackBufferはSwapChainが内部所有するBufferへのCOM参照なので最初に手放す。
		ReleaseRenderTargets();
		if(rtvAllocator_ != nullptr && rtvDescriptors_.IsValid()) {
			static_cast<void>(rtvAllocator_->Free(rtvDescriptors_));
		}
		rtvDescriptors_ = {};
		rtvAllocator_ = nullptr;
		swapChain_.Reset();
		width_ = 0;
		height_ = 0;
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	// Windowの新しいClientサイズへBackBufferとRTVを再生成する
	/////////////////////////////////////////////////////////////////////////////////////////
	Result<void> SwapChain::Resize(ID3D12Device* const device, const uint32_t width, const uint32_t height) {
		// 最小化中の0サイズはWindow側で保留するが、境界側でも無効値を拒否する。
		if(device == nullptr || width == 0 || height == 0) {
			return std::unexpected(Error(ErrorCategory::Graphics, kInvalidArgument, "SwapChain resize arguments are invalid."));
		}
		// OSから同一サイズのWM_SIZEが届く場合、GPU同期後の不要な再確保を避ける。
		if(width == width_ && height == height_) {
			return {};
		}
		// ResizeBuffersは外部BackBuffer参照が残っていると失敗するため、全ComPtrを先に解放する。
		// GPU側の利用完了はGraphicsSystemがWaitForIdleで保証してから本関数を呼ぶ。
		ReleaseRenderTargets();
		const HRESULT result = swapChain_->ResizeBuffers(kBufferCount, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
		if(FAILED(result)) {
			return std::unexpected(MakeDirectXError(kResizeFailed, result, "Failed to resize swap chain buffers."));
		}
		// Resize成功後のみ保持サイズを更新し、新しいBufferに対応するRTVを再生成する。
		width_ = width;
		height_ = height;
		return CreateRenderTargets(device);
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	// 描画完了済みBackBufferを表示する
	/////////////////////////////////////////////////////////////////////////////////////////
	Result<void> SwapChain::Present(const bool enableVSync) {
		// VSync有効時は次の垂直帰線を待ち、無効時は即時Presentを要求する。
		// Tearing flagはSwapChain生成時に有効化していないためPresent flagは常に0とする。
		const HRESULT result = swapChain_->Present(enableVSync ? 1U : 0U, 0);
		if(FAILED(result)) {
			return std::unexpected(MakeDirectXError(kPresentFailed, result, "Failed to present swap chain."));
		}
		return {};
	}

	uint32_t SwapChain::GetCurrentFrameIndex() const noexcept { return swapChain_->GetCurrentBackBufferIndex(); }
	uint32_t SwapChain::GetWidth() const noexcept { return width_; }
	uint32_t SwapChain::GetHeight() const noexcept { return height_; }
	ID3D12Resource* SwapChain::GetCurrentBackBuffer() const noexcept { return backBuffers_[GetCurrentFrameIndex()].Get(); }

	D3D12_CPU_DESCRIPTOR_HANDLE SwapChain::GetCurrentRtv() const noexcept {
		// Heap先頭から現在のBackBuffer index分だけDescriptor増分を加算する。
		return rtvAllocator_->GetCpuHandle(rtvDescriptors_, GetCurrentFrameIndex());
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	// SwapChain内の全BackBufferを取得して専用RTVを生成する
	/////////////////////////////////////////////////////////////////////////////////////////
	Result<void> SwapChain::CreateRenderTargets(ID3D12Device* const device) {
		// RTV Heapは連続配置なので、先頭HandleをDescriptor sizeずつ進めて各Bufferへ対応させる。
		for(uint32_t index = 0; index < kBufferCount; ++index) {
			const HRESULT result = swapChain_->GetBuffer(index, IID_PPV_ARGS(&backBuffers_[index]));
			if(FAILED(result)) {
				ReleaseRenderTargets();
				return std::unexpected(MakeDirectXError(kBackBufferAcquisitionFailed, result, "Failed to acquire swap chain back buffer."));
			}
			// nullptr DescriptorによりResource formatと一致する既定RTVを生成する。
			// CreateRenderTargetViewはvoid APIのため、事前にDeviceとResourceの有効性を保証する。
			device->CreateRenderTargetView(backBuffers_[index].Get(), nullptr, rtvAllocator_->GetCpuHandle(rtvDescriptors_, index));
		}
		return {};
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	// 所有するBackBufferへのCOM参照をすべて解放する
	/////////////////////////////////////////////////////////////////////////////////////////
	void SwapChain::ReleaseRenderTargets() noexcept {
		// 部分初期化時も全要素をResetし、再初期化可能な状態へ戻す。
		for(auto& backBuffer : backBuffers_) {
			backBuffer.Reset();
		}
	}

} // namespace NexusEngine
