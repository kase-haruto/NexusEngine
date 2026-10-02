#pragma once

// c++
#include <cstdint>
#include <span>

// engine
#include "Foundation/Error/Result.h"
#include "IndexBuffer.h"

struct ID3D12Device;

namespace NexusEngine {
	class CommandQueue;

	/*-----------------------------------------------------------------------------------------
	 * IndexBufferUploader
	 * - CPU Index byte列をUpload staging経由でDefault Heapへ転送する
	 * - stagingとCopy commandの寿命はFence完了まで保持する
	 *---------------------------------------------------------------------------------------*/
	class IndexBufferUploader final {
	public:
		/**
		 * \brief Index配列をGPUローカルBufferへ同期uploadする
		 * \param device Resource生成に使用する非所有Device
		 * \param commandQueue Copy実行とFence待機に使用する非所有Queue
		 * \param indexBuffer upload成功後のResource所有先
		 * \param data UInt16またはUInt32 Indexのbyte列
		 * \param format dataの1要素形式
		 */
		[[nodiscard]] Result<void> Upload(
			ID3D12Device* device,
			CommandQueue* commandQueue,
			IndexBuffer& indexBuffer,
			std::span<const uint8_t> data,
			IndexFormat format) const;
	};
} // namespace NexusEngine
