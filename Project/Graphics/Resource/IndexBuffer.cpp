#include "IndexBuffer.h"

// c++
#include <utility>

// directx
#include <d3d12.h>
#include <wrl/client.h>

namespace NexusEngine {
	namespace {
		[[nodiscard]] constexpr DXGI_FORMAT ToNativeIndexFormat(const IndexFormat format) noexcept {
			return format == IndexFormat::UInt32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
		}
	}

	class IndexBuffer::Impl final {
	public:
		Microsoft::WRL::ComPtr<ID3D12Resource> resource; //< GPU index fetch用Default Heap Buffer
		D3D12_INDEX_BUFFER_VIEW view = {}; //< Input Assemblerへ設定するNative View
		uint32_t indexCount = 0; //< DrawIndexedInstancedへ渡すIndex数
		IndexFormat format = IndexFormat::UInt16; //< CPU側が指定したIndex形式
	};

	IndexBuffer::IndexBuffer() noexcept = default;
	IndexBuffer::~IndexBuffer() noexcept { Shutdown(); }

	/////////////////////////////////////////////////////////////////////////////////////////
	// Upload完了後のResourceをIndex Buffer Viewと一体で所有する
	/////////////////////////////////////////////////////////////////////////////////////////
	void IndexBuffer::Commit(
		void* const nativeResource,
		const uint32_t sizeInBytes,
		const IndexFormat format,
		const uint32_t indexCount) {
		auto implementation = std::make_unique<Impl>();
		implementation->resource = static_cast<ID3D12Resource*>(nativeResource);
		implementation->view.BufferLocation = implementation->resource->GetGPUVirtualAddress();
		implementation->view.SizeInBytes = sizeInBytes;
		implementation->view.Format = ToNativeIndexFormat(format);
		implementation->indexCount = indexCount;
		implementation->format = format;
		impl_ = std::move(implementation);
	}

	void IndexBuffer::Bind(void* const nativeCommandList) const noexcept {
		if(impl_) {
			static_cast<ID3D12GraphicsCommandList*>(nativeCommandList)->IASetIndexBuffer(&impl_->view);
		}
	}

	void IndexBuffer::Shutdown() noexcept { impl_.reset(); }
	bool IndexBuffer::IsInitialized() const noexcept { return impl_ != nullptr; }
	uint32_t IndexBuffer::GetIndexCount() const noexcept { return impl_ ? impl_->indexCount : 0; }
	IndexFormat IndexBuffer::GetFormat() const noexcept {
		return impl_ ? impl_->format : IndexFormat::UInt16;
	}
} // namespace NexusEngine
