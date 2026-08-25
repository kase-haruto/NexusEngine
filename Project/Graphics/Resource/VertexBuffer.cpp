#include "VertexBuffer.h"
#include <d3d12.h>
#include <utility>
#include <wrl/client.h>

namespace NexusEngine {
	class VertexBuffer::Impl final {
	public:
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;		 //< GPU vertex fetch用Default Heap Buffer
		D3D12_VERTEX_BUFFER_VIEW			   view		   = {}; //< Input Assemblerへ設定するNative View
		uint32_t							   vertexCount = 0;	 //< DrawInstancedへ渡す要素数
	};

	VertexBuffer::VertexBuffer() noexcept = default;
	VertexBuffer::~VertexBuffer() noexcept { Shutdown(); }
	void VertexBuffer::Commit(void* const nativeResource, const uint32_t sizeInBytes, const uint32_t stride, const uint32_t vertexCount) {
		auto implementation					= std::make_unique<Impl>();
		implementation->resource			= static_cast<ID3D12Resource*>(nativeResource);
		implementation->view.BufferLocation = implementation->resource->GetGPUVirtualAddress();
		implementation->view.SizeInBytes	= sizeInBytes;
		implementation->view.StrideInBytes	= stride;
		implementation->vertexCount			= vertexCount;
		impl_								= std::move(implementation);
	}
	void VertexBuffer::Bind(void* const nativeCommandList) const noexcept {
		if(impl_) static_cast<ID3D12GraphicsCommandList*>(nativeCommandList)->IASetVertexBuffers(0, 1, &impl_->view);
	}
	void	 VertexBuffer::Shutdown() noexcept { impl_.reset(); }
	bool	 VertexBuffer::IsInitialized() const noexcept { return impl_ != nullptr; }
	uint32_t VertexBuffer::GetVertexCount() const noexcept { return impl_ ? impl_->vertexCount : 0; }
} // namespace NexusEngine
