#pragma once

// c++
#include <cstdint>
#include <span>

// engine
#include "Foundation/Error/Result.h"

struct ID3D12Device;

namespace NexusEngine {
	class CommandQueue;
	class VertexBuffer;

	/*-----------------------------------------------------------------------------------------
	 * VertexBufferUploader
	 * - 静的Vertex byte列をUpload staging経由でDefault Heap Bufferへ転送する
	 * - VertexBuffer本体へCPU mappingやCommand記録責務を持ち込まない
	 *---------------------------------------------------------------------------------------*/
	class VertexBufferUploader final {
	public:
		[[nodiscard]] Result<void> Upload(
			ID3D12Device*			 device,
			CommandQueue*			 commandQueue,
			VertexBuffer&			 vertexBuffer,
			std::span<const uint8_t> data,
			uint32_t				 stride) const;
	};
} // namespace NexusEngine
