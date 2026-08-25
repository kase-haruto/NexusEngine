#pragma once

// directx
#include <d3d12.h>
#include <wrl/client.h>

// engine
#include "Foundation/Error/Result.h"

namespace NexusEngine {
	class CommandQueue;

	/*-----------------------------------------------------------------------------------------
	 * ImmediateUploadContext
	 * - 初期化時の同期Uploadで共有する一時CommandAllocatorとCommandListを所有する
	 * - Resource生成、copy内容、Resource寿命、非同期streamingは担当しない
	 *---------------------------------------------------------------------------------------*/
	class ImmediateUploadContext final {
	public:
		/** \brief 記録状態のDirect Command Listと専用Allocatorを生成する */
		[[nodiscard]] Result<void> Initialize(ID3D12Device* device);
		/**
		 * \brief Command Listを閉じてQueueへ投入し、Fence完了まで待機する
		 * \note 呼び出し側は戻るまでUpload stagingと転送先Resourceを保持する
		 */
		[[nodiscard]] Result<void>				 ExecuteAndWait(CommandQueue* commandQueue);
		[[nodiscard]] ID3D12GraphicsCommandList* GetCommandList() const noexcept { return commandList_.Get(); }

	private:
		Microsoft::WRL::ComPtr<ID3D12CommandAllocator>	  allocator_;	//< Upload List専用Allocator
		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_; //< copyとbarrierの記録先
	};
} // namespace NexusEngine
