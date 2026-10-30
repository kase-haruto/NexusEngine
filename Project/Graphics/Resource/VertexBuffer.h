#pragma once
#include <cstdint>
#include <memory>

namespace NexusEngine {
	class GraphicsContext;
	class VertexBufferUploader;

	/*-----------------------------------------------------------------------------------------
	 * VertexBuffer
	 * - Upload完了済みDefault Heap Vertex ResourceとViewを所有する
	 * - CPU data、staging、Command記録、Primitive形状、Pipelineは担当しない
	 *---------------------------------------------------------------------------------------*/
	class VertexBuffer final {
	public:
		VertexBuffer() noexcept;
		~VertexBuffer() noexcept;
		VertexBuffer(const VertexBuffer&)				 = delete;
		VertexBuffer& operator=(const VertexBuffer&)	 = delete;
		VertexBuffer(VertexBuffer&&)					 = delete;
		VertexBuffer&		   operator=(VertexBuffer&&) = delete;
		void				   Shutdown() noexcept;
		[[nodiscard]] bool	   IsInitialized() const noexcept;
		[[nodiscard]] uint32_t GetVertexCount() const noexcept;

	private:
		friend class MeshResource;
		void ShareFrom(const VertexBuffer& source) noexcept;
		friend class GraphicsContext;
		friend class VertexBufferUploader;
		friend class RayTracingScene;
		friend class OpticalBackend;
		/** \brief AS BackendだけがGeometry Resourceを非所有参照する */
		[[nodiscard]] void* GetNativeResource() const noexcept;
		[[nodiscard]] uint32_t GetStride() const noexcept;
		void Commit(void* nativeResource, uint32_t sizeInBytes, uint32_t stride, uint32_t vertexCount);
		void Bind(void* nativeCommandList) const noexcept;
		class Impl;
		std::shared_ptr<Impl> impl_; //< 不変のGPU Bufferをアセットsnapshot間で共有する
	};
} // namespace NexusEngine
