#pragma once

// c++
#include <cstdint>
#include <vector>

// engine
#include "IndexBuffer.h"
#include "VertexBuffer.h"

namespace NexusEngine {
	class GraphicsContext;
	class GraphicsResourceFactory;

	/** 1つのMesh Buffer内で同じMaterialによって描画するIndex範囲。 */
	struct SubmeshRange {
		uint32_t firstIndex = 0;
		uint32_t indexCount = 0;
		int32_t vertexOffset = 0;
		uint32_t materialSlot = 0;
	};

	/*-----------------------------------------------------------------------------------------
	 * MeshResource
	 * - 1つのVertex/Index Buffer対と複数Submeshの描画範囲を所有する
	 * - Material本体、Model hierarchy、Skeleton、Animationは所有しない
	 *---------------------------------------------------------------------------------------*/
	class MeshResource final {
	public:
		MeshResource() noexcept = default;
		~MeshResource() noexcept;
		MeshResource(const MeshResource&) = delete;
		MeshResource& operator=(const MeshResource&) = delete;
		MeshResource(MeshResource&&) = delete;
		MeshResource& operator=(MeshResource&&) = delete;

		/** \brief Vertex/Index BufferとSubmesh metadataを破棄する */
		void Shutdown() noexcept;
		[[nodiscard]] bool IsInitialized() const noexcept;
		[[nodiscard]] const std::vector<SubmeshRange>& GetSubmeshes() const noexcept;

	private:
		friend class GraphicsContext;
		friend class GraphicsResourceFactory;

		VertexBuffer vertexBuffer_;
		IndexBuffer indexBuffer_;
		std::vector<SubmeshRange> submeshes_;
	};
} // namespace NexusEngine
