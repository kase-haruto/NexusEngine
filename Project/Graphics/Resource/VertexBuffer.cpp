#include "VertexBuffer.h"

#include <cstring>
#include <limits>

namespace NexusEngine {
	namespace { constexpr int32_t kInvalidArgument = 1; constexpr int32_t kCreationFailed = 2; constexpr int32_t kMapFailed = 3; }

	Result<void> VertexBuffer::Initialize(ID3D12Device* const device, const std::span<const uint8_t> data, const uint32_t stride) {
		if(device == nullptr || data.empty() || stride == 0 || data.size() % stride != 0 ||
		   data.size() > (std::numeric_limits<UINT>::max)() ||
		   data.size() / stride > (std::numeric_limits<uint32_t>::max)()) {
			return std::unexpected(Error(ErrorCategory::Graphics, kInvalidArgument, "Vertex buffer arguments are invalid."));
		}
		// Primitive確認段階ではUpload Heap常駐とし、Upload Command経路をVertexBufferへ混在させない。
		// 静的Model導入時はDefault Heapと専用Upload処理へ差し替える。
		D3D12_HEAP_PROPERTIES heapProperties = {};
		heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Width = data.size();
		desc.Height = 1;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.SampleDesc.Count = 1;
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		HRESULT result = device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&resource_));
		if(FAILED(result)) return std::unexpected(MakeDirectXError(kCreationFailed, result, "Failed to create vertex buffer."));

		void* mapped = nullptr;
		const D3D12_RANGE readRange { 0, 0 };
		result = resource_->Map(0, &readRange, &mapped);
		if(FAILED(result)) return std::unexpected(MakeDirectXError(kMapFailed, result, "Failed to map vertex buffer."));
		std::memcpy(mapped, data.data(), data.size());
		resource_->Unmap(0, nullptr);

		view_.BufferLocation = resource_->GetGPUVirtualAddress();
		view_.SizeInBytes = static_cast<UINT>(data.size());
		view_.StrideInBytes = stride;
		vertexCount_ = static_cast<uint32_t>(data.size() / stride);
		return {};
	}

	void VertexBuffer::Bind(ID3D12GraphicsCommandList* const commandList) const noexcept {
		commandList->IASetVertexBuffers(0, 1, &view_);
	}
	void VertexBuffer::Shutdown() noexcept {
		resource_.Reset();
		view_ = {};
		vertexCount_ = 0;
	}
	uint32_t VertexBuffer::GetVertexCount() const noexcept { return vertexCount_; }

} // namespace NexusEngine
