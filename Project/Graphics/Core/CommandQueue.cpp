#include "CommandQueue.h"
#include <limits>

namespace NexusEngine {
	namespace {
		constexpr int32_t kInvalidDevice = 1;
		constexpr int32_t kQueueCreationFailed = 2;
		constexpr int32_t kFenceCreationFailed = 3;
		constexpr int32_t kEventCreationFailed = 4;
		constexpr int32_t kSignalFailed = 5;
		constexpr int32_t kWaitRegistrationFailed = 6;
		constexpr int32_t kFenceWaitFailed = 7;
	}

	CommandQueue::~CommandQueue() noexcept { Shutdown(); }

	/////////////////////////////////////////////////////////////////////////////////////////
	// Direct Command QueueとGPU同期オブジェクトを初期化する
	/////////////////////////////////////////////////////////////////////////////////////////
	Result<void> CommandQueue::Initialize(ID3D12Device* const device) {
		// Deviceがなければ後続の生成APIを呼べないため、Graphics境界で早期に拒否する。
		if(device == nullptr) {
			return std::unexpected(Error(ErrorCategory::Graphics, kInvalidDevice, "CommandQueue requires a valid device."));
		}

		// Clear、Barrier、将来のDrawを扱えるDirect Queueを生成する。
		// Priority、NodeMask、Flagsは標準動作を使用し、必要になるまで設定項目を増やさない。
		D3D12_COMMAND_QUEUE_DESC desc = {};
		desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		HRESULT result = device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue_));
		if(FAILED(result)) {
			Shutdown();
			return std::unexpected(MakeDirectXError(kQueueCreationFailed, result, "Failed to create command queue."));
		}

		// 初期完了値0のFenceをQueueと対で所有し、FrameContext再利用位置を追跡する。
		result = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
		if(FAILED(result)) {
			Shutdown();
			return std::unexpected(MakeDirectXError(kFenceCreationFailed, result, "Failed to create command queue fence."));
		}

		return {};
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	// CPU同期オブジェクトとQueueを依存の逆順で解放する
	/////////////////////////////////////////////////////////////////////////////////////////
	void CommandQueue::Shutdown() noexcept {
		// FenceはQueueの実行位置を表すため、Queueより先に参照を解放する。
		fence_.Reset();
		queue_.Reset();
		nextFenceValue_ = 1;
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	// 記録済みCommand ListをQueueへ投入する
	/////////////////////////////////////////////////////////////////////////////////////////
	void CommandQueue::Execute(ID3D12CommandList* const commandList) noexcept {
		// ExecuteCommandListsは配列を要求するため、単一Listも一要素配列として渡す。
		// QueueはCommand Listを所有せず、GPU実行完了まではFrameContextが寿命を保持する。
		ID3D12CommandList* commandLists[] = { commandList };
		queue_->ExecuteCommandLists(1, commandLists);
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	// Queue末尾へ単調増加するFence値を記録する
	/////////////////////////////////////////////////////////////////////////////////////////
	Result<uint64_t> CommandQueue::Signal() {
		const std::lock_guard lock(signalMutex_);
		// 値を先に確保し、成功時に呼び出し側が同じ値をFrameContextへ保存できるようにする。
		const uint64_t fenceValue = nextFenceValue_++;
		const HRESULT result = queue_->Signal(fence_.Get(), fenceValue);
		if(FAILED(result)) {
			return std::unexpected(MakeDirectXError(kSignalFailed, result, "Failed to signal command queue fence."));
		}
		return fenceValue;
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	// 指定Fence値に対応するGPU処理の完了を待機する
	/////////////////////////////////////////////////////////////////////////////////////////
	Result<void> CommandQueue::Wait(const uint64_t fenceValue) {
		// 未投入を表す0と既に完了した値ではKernel Eventを使わず即時復帰する。
		const uint64_t completed = fence_->GetCompletedValue();
		// UINT64_MAX is the device-removal sentinel, not proof that pending GPU work completed.
		// Returning it as successful synchronization could allow AS/Descriptor reuse after removal.
		if(completed == (std::numeric_limits<uint64_t>::max)()) {
			return std::unexpected(MakeDirectXError(kFenceWaitFailed, DXGI_ERROR_DEVICE_REMOVED, "Device removed while waiting for GPU fence."));
		}
		if(fenceValue == 0 || completed >= fenceValue) {
			return {};
		}
		// 描画とアセット準備が同時に待機できるよう、スレッドごとにEventを持つ。
		// 自動リセットEventを共有すると、別の待機スレッドを起こすおそれがある。
		struct WaitEvent {
			HANDLE handle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
			~WaitEvent() { if(handle) CloseHandle(handle); }
		};
		thread_local WaitEvent event;
		if(!event.handle) return std::unexpected(Error(ErrorCategory::Graphics, kEventCreationFailed,
			"Failed to create fence event.", static_cast<Error::NativeErrorCode>(GetLastError())));
		const HRESULT result = fence_->SetEventOnCompletion(fenceValue, event.handle);
		if(FAILED(result)) {
			return std::unexpected(MakeDirectXError(kWaitRegistrationFailed, result, "Failed to register fence completion event."));
		}
		// FrameContextのAllocatorを安全にResetするため、対象GPU処理の完了まで待機する。
		const DWORD waitResult = WaitForSingleObject(event.handle, INFINITE);
		if(waitResult != WAIT_OBJECT_0) {
			return std::unexpected(Error(ErrorCategory::Graphics, kFenceWaitFailed, "GPU Fence wait failed.",
				static_cast<Error::NativeErrorCode>(GetLastError())));
		}
		if(fence_->GetCompletedValue() == (std::numeric_limits<uint64_t>::max)()) {
			return std::unexpected(MakeDirectXError(kFenceWaitFailed, DXGI_ERROR_DEVICE_REMOVED, "Device removed during GPU fence wait."));
		}
		return {};
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	// Queueの現在末尾へFenceを置き、そこまでの全GPU処理を待機する
	/////////////////////////////////////////////////////////////////////////////////////////
	Result<void> CommandQueue::WaitForIdle() {
		// 新しいFence値をQueue末尾に積むことで、それ以前の全Command完了を表現する。
		auto signalResult = Signal();
		if(!signalResult) {
			return std::unexpected(std::move(signalResult.error()));
		}
		// Signal成功時だけ同じ値をCPU待機へ渡し、終了・Resize時の安全性を保証する。
		return Wait(*signalResult);
	}

	uint64_t CommandQueue::GetCompletedFenceValue() const noexcept {
		return fence_ ? fence_->GetCompletedValue() : 0;
	}

	ID3D12CommandQueue* CommandQueue::GetNativeQueue() const noexcept { return queue_.Get(); }

} // namespace NexusEngine
