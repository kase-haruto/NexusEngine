#include "GraphicsSystem.h"

// c++
#include <utility>

// directx
#include <d3d12.h>
#include <wrl/client.h>

// engine
#include "Foundation/Logging/Logger.h"
#include "Graphics/Core/CommandQueue.h"
#include "Graphics/Descriptor/BindlessDescriptorTable.h"
#include "Graphics/Descriptor/DescriptorManager.h"
#include "Graphics/Descriptor/TransientDescriptorArena.h"
#include "Graphics/Device/GraphicsDevice.h"
#include "Graphics/Presentation/SwapChain.h"
#include "GraphicsContext.h"
#include "GraphicsRenderer.h"
#include "GraphicsResourceFactory.h"
#include "GraphicsRenderExtension.h"

namespace NexusEngine {
	namespace {
		constexpr int32_t kAlreadyInitialized = 1;
		constexpr int32_t kNotInitialized = 2;
		constexpr int32_t kAllocatorCreationFailed = 3;
		constexpr int32_t kCommandListCreationFailed = 4;
		constexpr int32_t kCommandListCloseFailed = 5;
		constexpr int32_t kAllocatorResetFailed = 6;
		constexpr int32_t kCommandListResetFailed = 7;
	}

	class GraphicsSystem::Impl final {
	public:
		struct FrameContext {
			Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator; //< このBackBuffer用Allocator
			Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList; //< 描画命令記録先
			uint64_t fenceValue = 0; //< 前回このContextを投入したFence値
		};

		GraphicsDevice device;
		CommandQueue commandQueue;
		DescriptorManager descriptors;
		SwapChain swapChain;
		BindlessDescriptorTable bindlessDescriptors;
		TransientDescriptorArena transientDescriptors;
		GraphicsResourceFactory resourceFactory;
		std::array<FrameContext, SwapChain::kBufferCount> frames;
		std::array<float, 4> clearColor = {};
		std::filesystem::path shaderDirectory;
		bool enableVSync = true;
		void* nativeWindow = nullptr;
		IGraphicsRenderer* renderer = nullptr;
		IGraphicsRenderExtension* renderExtension = nullptr;
	};

	GraphicsSystem::GraphicsSystem() noexcept = default;
	GraphicsSystem::~GraphicsSystem() noexcept { Shutdown(); }

	/////////////////////////////////////////////////////////////////////////////////////////
	// Graphicsサブシステムを依存順に初期化する
	/////////////////////////////////////////////////////////////////////////////////////////
	Result<void> GraphicsSystem::Initialize(const WindowSurfaceDesc& surface, const GraphicsSystemDesc& desc) {
		// PImplの存在を初期化済み状態として扱い、COMオブジェクトの二重生成を防ぐ。
		if(impl_) {
			return std::unexpected(Error(ErrorCategory::Graphics, kAlreadyInitialized, "GraphicsSystem is already initialized."));
		}

		// 初期化完了まではローカル所有とし、途中失敗時はImplのRAIIで部分初期化状態を破棄する。
		auto implementation = std::make_unique<Impl>();

		// 上位公開設定をDevice専用設定へ変換し、SwapChainやFrame設定をDeviceへ渡さない。
		GraphicsDeviceDesc deviceDesc;
		deviceDesc.enableDebugLayer = desc.enableDebugLayer;
		deviceDesc.enableGpuBasedValidation = desc.enableGpuBasedValidation;
		deviceDesc.enableDred = desc.enableDred;
		deviceDesc.useWarpAdapter = desc.useWarpAdapter;

		// FactoryとDeviceを最初に生成し、後続要素は非所有Device参照だけを受け取る。
		auto result = implementation->device.Initialize(deviceDesc);
		if(!result) {
			return std::unexpected(std::move(result.error()));
		}
		// SwapChain生成にはQueueが必要なので、Deviceの次にDirect QueueとFenceを確立する。
		result = implementation->commandQueue.Initialize(implementation->device.GetDevice());
		if(!result) {
			return std::unexpected(std::move(result.error()));
		}
		result = implementation->descriptors.Initialize(
			implementation->device.GetDevice(), desc.bindlessResourceCapacity,
			desc.rtvDescriptorCapacity, desc.dsvDescriptorCapacity, desc.bindlessSamplerCapacity);
		if(!result) return std::unexpected(std::move(result.error()));

		// ProjectWindowそのものではなく、Frameworkが抽出したSurface情報だけで表示経路を生成する。
		result = implementation->swapChain.Initialize(
			implementation->device.GetFactory(), implementation->device.GetDevice(),
			implementation->commandQueue.GetNativeQueue(), surface.nativeHandle, surface.width, surface.height,
			&implementation->descriptors.RenderTargets());
		if(!result) {
			return std::unexpected(std::move(result.error()));
		}

		// BackBufferごとにAllocatorとCommand Listを分離し、別フレームのGPU実行中でも
		// CPUが次の利用可能なFrameContextへ命令を記録できる構造にする。
		for(auto& frame : implementation->frames) {
			HRESULT nativeResult = implementation->device.GetDevice()->CreateCommandAllocator(
				D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frame.allocator));
			if(FAILED(nativeResult)) {
				return std::unexpected(MakeDirectXError(kAllocatorCreationFailed, nativeResult, "Failed to create frame command allocator."));
			}
			// Command Listは対応Allocatorを初期状態として生成し、フレームごとに同じ組を再利用する。
			nativeResult = implementation->device.GetDevice()->CreateCommandList(
				0, D3D12_COMMAND_LIST_TYPE_DIRECT, frame.allocator.Get(), nullptr, IID_PPV_ARGS(&frame.commandList));
			if(FAILED(nativeResult)) {
				return std::unexpected(MakeDirectXError(kCommandListCreationFailed, nativeResult, "Failed to create frame command list."));
			}
			// CreateCommandList直後は記録状態なので、最初のRenderFrameでReset可能な閉状態へ移す。
			nativeResult = frame.commandList->Close();
			if(FAILED(nativeResult)) {
				return std::unexpected(MakeDirectXError(kCommandListCloseFailed, nativeResult, "Failed to close initial command list."));
			}
		}

		// Bindless TableがResource/Sampler Heap、null slot、generationを一体で管理する。
		result = implementation->bindlessDescriptors.Initialize(
			implementation->device.GetDevice(), &implementation->descriptors);
		if(!result) return std::unexpected(std::move(result.error()));

		// null slotを含む永続Bindless領域の確立後に、残りの同一HeapからFrame専用領域を予約する。
		// 容量不足を起動時に検出することで、描画途中の部分的な初期化を避ける。
		result = implementation->transientDescriptors.Initialize(
			implementation->device.GetDevice(), &implementation->descriptors.Resources(),
			SwapChain::kBufferCount, desc.transientResourceDescriptorsPerFrame);
		if(!result) return std::unexpected(std::move(result.error()));

		// RendererへNative Deviceを公開せずResourceを生成できるよう、Device寿命に従うFactoryを用意する。
		// Factory自体はResourceを所有せず、生成物は接続されたRendererが明示的に破棄する。
		implementation->resourceFactory.ConnectBackend(
			implementation->device.GetDevice(), &implementation->commandQueue,
			&implementation->bindlessDescriptors, SwapChain::kBufferCount);

		// 全必須リソースが完成してから実行時設定とPImplを公開する。
		implementation->clearColor = desc.clearColor;
		implementation->shaderDirectory = desc.shaderDirectory;
		implementation->enableVSync = desc.enableVSync;
		implementation->nativeWindow = surface.nativeHandle;
		impl_ = std::move(implementation);
		NEXUS_LOG_INFO("Graphics", "GraphicsSystem initialized with double-buffered frame contexts.");
		return {};
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	// GPU完了後に依存側からGraphicsリソースを破棄する
	/////////////////////////////////////////////////////////////////////////////////////////
	void GraphicsSystem::Shutdown() noexcept {
		// 未初期化または二重Shutdownでは何も行わず、デストラクタからも安全に呼べるようにする。
		if(!impl_) {
			return;
		}
		// GPUがBackBufferやCommand資源を参照し終えるまで待ってからCOM参照を解放する。
		auto waitResult = impl_->commandQueue.WaitForIdle();
		if(!waitResult) {
			NEXUS_LOG_ERROR("Graphics", waitResult.error().GetMessageText());
		}
		if(impl_->renderExtension != nullptr) {
			impl_->renderExtension->Shutdown();
			impl_->renderExtension = nullptr;
		}
		if(impl_->renderer != nullptr) {
			impl_->renderer->Shutdown();
			impl_->renderer = nullptr;
		}
		// Command ListはAllocatorを参照して生成されるため、Listから先に解放する。
		for(auto& frame : impl_->frames) {
			frame.commandList.Reset();
			frame.allocator.Reset();
			frame.fenceValue = 0;
		}
		// Renderer Resourceの破棄完了後に、非所有Device参照だけを持つFactoryを破棄する。
		impl_->resourceFactory.ConnectBackend(nullptr, nullptr, nullptr, 0);
		impl_->transientDescriptors.Shutdown();
		impl_->bindlessDescriptors.Shutdown();
		// 表示資源からDeviceへ向かって初期化と逆順に破棄する。
		impl_->swapChain.Shutdown();
		impl_->descriptors.Shutdown();
		impl_->commandQueue.Shutdown();
		impl_->device.Shutdown();
		impl_.reset();
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	// 現在のBackBufferをClearしてPresentする
	/////////////////////////////////////////////////////////////////////////////////////////
	Result<void> GraphicsSystem::RenderFrame() {
		// 公開API境界で未初期化利用を検出し、nullptrアクセスを防止する。
		if(!impl_) {
			return std::unexpected(Error(ErrorCategory::Graphics, kNotInitialized, "GraphicsSystem is not initialized."));
		}

		// DXGIが示す現在のBackBuffer indexと同じFrameContextを選択する。
		const uint32_t frameIndex = impl_->swapChain.GetCurrentFrameIndex();
		auto& frame = impl_->frames[frameIndex];
		// 完了Fenceへ到達したBindless slotだけを回収し、GPU参照中のindex再利用を防ぐ。
		impl_->bindlessDescriptors.CollectGarbage(impl_->commandQueue.GetCompletedFenceValue());
		// 同じBackBuffer用AllocatorをGPUが使用中の場合だけ待つ。毎フレームの全面同期は行わない。
		auto waitResult = impl_->commandQueue.Wait(frame.fenceValue);
		if(!waitResult) {
			return waitResult;
		}
		// このFrameが以前発行したDrawは完了済みなので、一時Descriptor領域を安全に再利用できる。
		// ResetをFence待機より前に移動するとGPUが参照中のDescriptorを書き換えるため順序を維持する。
		impl_->transientDescriptors.ResetFrame(frameIndex);

		// 対応Fenceの完了後なので、このAllocatorが保持していた前回の記録領域を再利用できる。
		HRESULT result = frame.allocator->Reset();
		if(FAILED(result)) {
			return std::unexpected(MakeDirectXError(kAllocatorResetFailed, result, "Failed to reset frame command allocator."));
		}
		// Clear段階ではPipeline Stateを使用しないため、初期PSOにはnullptrを指定する。
		result = frame.commandList->Reset(frame.allocator.Get(), nullptr);
		if(FAILED(result)) {
			return std::unexpected(MakeDirectXError(kCommandListResetFailed, result, "Failed to reset frame command list."));
		}

		// Present状態のBackBufferへRTV操作を行えるよう、Render Target状態へ遷移させる。
		// State管理はDirectX Backend内部に留め、Frameworkや将来のSceneへ公開しない。
		ID3D12Resource* const backBuffer = impl_->swapChain.GetCurrentBackBuffer();
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = backBuffer;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		frame.commandList->ResourceBarrier(1, &barrier);

		// 現在Bufferに対応するRTVだけをOutput Mergerへ設定し、設定色で全面Clearする。
		const D3D12_CPU_DESCRIPTOR_HANDLE rtv = impl_->swapChain.GetCurrentRtv();
		frame.commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
		frame.commandList->ClearRenderTargetView(rtv, impl_->clearColor.data(), 0, nullptr);

		// SM 6.6 ShaderがDirect Heap Indexingで参照するResource/Sampler Heapを設定する。
		impl_->bindlessDescriptors.Bind(frame.commandList.Get());

		// ViewportとScissorは現在のSwapChainサイズから毎フレーム構築し、Resize後の値を即時反映する。
		const D3D12_VIEWPORT viewport {
			0.0f, 0.0f, static_cast<float>(impl_->swapChain.GetWidth()),
			static_cast<float>(impl_->swapChain.GetHeight()), 0.0f, 1.0f
		};
		const D3D12_RECT scissor {
			0, 0, static_cast<LONG>(impl_->swapChain.GetWidth()), static_cast<LONG>(impl_->swapChain.GetHeight())
		};
		frame.commandList->RSSetViewports(1, &viewport);
		frame.commandList->RSSetScissorRects(1, &scissor);
		// Frame中だけ有効なContextへCommand Listを隠し、上位RendererにはBackend非依存命令だけを公開する。
		GraphicsContext graphicsContext(
			frame.commandList.Get(), &impl_->bindlessDescriptors,
			&impl_->transientDescriptors, frameIndex);
		if(impl_->renderer != nullptr) {
			impl_->renderer->Render(graphicsContext);
		}
		if(impl_->renderExtension != nullptr) {
			impl_->renderExtension->BeginFrame();
			impl_->renderExtension->Record(frame.commandList.Get());
		}

		// Present可能な状態へ戻してからCommand Listを閉じ、Queueへ投入する。
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
		frame.commandList->ResourceBarrier(1, &barrier);
		result = frame.commandList->Close();
		if(FAILED(result)) {
			return std::unexpected(MakeDirectXError(kCommandListCloseFailed, result, "Failed to close frame command list."));
		}
		// Close成功後のListだけをQueueへ投入し、記録資源はFrameContextが所有し続ける。
		impl_->commandQueue.Execute(frame.commandList.Get());
		// Queue投入済みCommandの完了位置は、後続のPresent成否にかかわらず先に保存する。
		auto signalResult = impl_->commandQueue.Signal();
		if(!signalResult) {
			return std::unexpected(std::move(signalResult.error()));
		}
		frame.fenceValue = *signalResult;

		// 描画CommandのFence値を確定した後に、DXGIへ表示を要求する。
		auto presentResult = impl_->swapChain.Present(impl_->enableVSync);
		if(!presentResult) {
			return presentResult;
		}
		return {};
	}

	Result<void> GraphicsSystem::Resize(const uint32_t width, const uint32_t height) {
		// WindowイベントはGraphics初期化後にだけ処理されるが、API境界でも状態を検証する。
		if(!impl_) {
			return std::unexpected(Error(ErrorCategory::Graphics, kNotInitialized, "GraphicsSystem is not initialized."));
		}
		// ResizeBuffers前に全BackBuffer参照がGPUから解放済みであることを保証する。
		auto waitResult = impl_->commandQueue.WaitForIdle();
		if(!waitResult) {
			return waitResult;
		}
		// Queue全体の完了後は過去のFrame Fence値を再待機する必要がないため初期値へ戻す。
		for(auto& frame : impl_->frames) {
			frame.fenceValue = 0;
		}
		// DeviceはRTV再生成にだけ非所有参照として渡し、SwapChainがDeviceを所有しない構造を保つ。
		return impl_->swapChain.Resize(impl_->device.GetDevice(), width, height);
	}

	void GraphicsSystem::SetClearColor(const std::array<float, 4>& color) noexcept {
		// 初期化前の設定はDesc経由とし、実行中のPImplがある場合だけ即時反映する。
		if(impl_) {
			impl_->clearColor = color;
		}
	}

	Result<void> GraphicsSystem::AttachRenderer(IGraphicsRenderer* const renderer) {
		if(!impl_ || renderer == nullptr || impl_->renderer != nullptr || impl_->resourceFactory.nativeDevice_ == nullptr) {
			return std::unexpected(Error(ErrorCategory::Graphics, kNotInitialized, "Graphics renderer cannot be attached."));
		}

		// 初期化に成功するまで非所有pointerを公開せず、失敗時に半接続状態を残さない。
		const GraphicsRendererInitializationContext context {
			.resources = impl_->resourceFactory,
			.shaderDirectory = impl_->shaderDirectory
		};
		auto result = renderer->Initialize(context);
		if(!result) {
			return result;
		}
		impl_->renderer = renderer;
		return {};
	}

	Result<void> GraphicsSystem::AttachRenderExtension(IGraphicsRenderExtension* const extension) {
		if(!impl_ || extension == nullptr || impl_->renderExtension != nullptr) {
			return std::unexpected(Error(ErrorCategory::Graphics, kNotInitialized, "Render extension cannot be attached."));
		}
		GraphicsRenderExtensionContext context;
		context.device = impl_->device.GetDevice();
		context.commandQueue = impl_->commandQueue.GetNativeQueue();
		context.resourceDescriptors = &impl_->descriptors.Resources();
		context.nativeWindow = impl_->nativeWindow;
		context.framesInFlight = SwapChain::kBufferCount;
		context.renderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
		auto result = extension->Initialize(context);
		if(!result) return result;
		impl_->renderExtension = extension;
		return {};
	}

} // namespace NexusEngine
