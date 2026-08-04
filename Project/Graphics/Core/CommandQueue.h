#pragma once

// c++
#include <cstdint>

// windows
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <d3d12.h>
#include <wrl/client.h>

// engine
#include "Foundation/Error/Result.h"

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * CommandQueue
	 * - Direct Command QueueとFenceを所有し、GPU実行と同期を担当する
	 * - CommandAllocator、CommandList、描画命令は管理しない
	 *---------------------------------------------------------------------------------------*/
	class CommandQueue final {
	public:
		CommandQueue() noexcept = default;
		~CommandQueue() noexcept;
		CommandQueue(const CommandQueue&) = delete;
		CommandQueue& operator=(const CommandQueue&) = delete;

		/**
		 * \brief Direct Command QueueとGPU同期用Fenceを初期化する
		 * \param device QueueとFenceの生成に使用する非所有Device
		 * \return 初期化結果
		 */
		[[nodiscard]] Result<void> Initialize(ID3D12Device* device);
		/**
		 * \brief Fenceイベント、Fence、Command Queueを解放する
		 * \note GPU完了待機は呼び出し側がWaitForIdleで保証する
		 */
		void Shutdown() noexcept;
		/**
		 * \brief 記録済みCommand ListをGPU実行Queueへ投入する
		 * \param commandList 実行する閉じたCommand Listの非所有参照
		 */
		void Execute(ID3D12CommandList* commandList) noexcept;
		/**
		 * \brief Queueへ新しいFence値をSignalする
		 * \return SignalしたFence値またはエラー
		 */
		[[nodiscard]] Result<uint64_t> Signal();
		/**
		 * \brief 指定Fence値までGPU処理が完了するのをCPU側で待機する
		 * \param fenceValue 待機対象のFence値
		 * \return 待機結果
		 */
		[[nodiscard]] Result<void> Wait(uint64_t fenceValue);
		/**
		 * \brief 現在までにQueueへ投入された全処理の完了を待機する
		 * \return GPU待機結果
		 */
		[[nodiscard]] Result<void> WaitForIdle();

		/**
		 * \brief Backend内部で使用するCommand Queueを取得する
		 * \return 所有権を持たないCommand Queue
		 */
		[[nodiscard]] ID3D12CommandQueue* GetNativeQueue() const noexcept;

	private:
		Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_; //< GPUへCommand Listを投入するQueue
		Microsoft::WRL::ComPtr<ID3D12Fence> fence_;       //< Queue完了位置を追跡するFence
		HANDLE fenceEvent_ = nullptr;                     //< CPU待機用イベント
		uint64_t nextFenceValue_ = 1;                     //< 次回Signalへ割り当てる単調増加値
	};

} // namespace NexusEngine
