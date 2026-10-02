#include "IndexBufferUploader.h"

// c++
#include <cstring>
#include <limits>

// directx
#include <d3d12.h>
#include <wrl/client.h>

// engine
#include "ImmediateUploadContext.h"

namespace NexusEngine {
	namespace {
		constexpr int32_t kInvalidArgument = 1;
		constexpr int32_t kDestinationCreationFailed = 2;
		constexpr int32_t kUploadCreationFailed = 3;
		constexpr int32_t kUploadMapFailed = 4;

		[[nodiscard]] constexpr uint32_t GetIndexStride(const IndexFormat format) noexcept {
			return format == IndexFormat::UInt32 ? sizeof(uint32_t) : sizeof(uint16_t);
		}
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	// CPU Index列をstagingへ書き込み、Default HeapへCopy後にIndex参照状態へ遷移する
	/////////////////////////////////////////////////////////////////////////////////////////
	Result<void> IndexBufferUploader::Upload(
		ID3D12Device* const device,
		CommandQueue* const commandQueue,
		IndexBuffer& indexBuffer,
		const std::span<const uint8_t> data,
		const IndexFormat format) const {
		const uint32_t stride = GetIndexStride(format);
		if(device == nullptr || commandQueue == nullptr || indexBuffer.IsInitialized() ||
		   data.empty() || data.size() % stride != 0 ||
		   data.size() > (std::numeric_limits<UINT>::max)() ||
		   data.size() / stride > (std::numeric_limits<uint32_t>::max)()) {
			return std::unexpected(Error(ErrorCategory::Graphics, kInvalidArgument,
				"Index buffer upload arguments or state are invalid."));
		}

		D3D12_RESOURCE_DESC bufferDesc = {};
		bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		bufferDesc.Width = data.size();
		bufferDesc.Height = 1;
		bufferDesc.DepthOrArraySize = 1;
		bufferDesc.MipLevels = 1;
		bufferDesc.SampleDesc.Count = 1;
		bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		D3D12_HEAP_PROPERTIES defaultHeap = {};
		defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
		Microsoft::WRL::ComPtr<ID3D12Resource> destination;
		HRESULT result = device->CreateCommittedResource(
			&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
			D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&destination));
		if(FAILED(result)) {
			return std::unexpected(MakeDirectXError(kDestinationCreationFailed, result,
				"Failed to create default heap index buffer."));
		}

		D3D12_HEAP_PROPERTIES uploadHeap = {};
		uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
		Microsoft::WRL::ComPtr<ID3D12Resource> uploadResource;
		result = device->CreateCommittedResource(
			&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadResource));
		if(FAILED(result)) {
			return std::unexpected(MakeDirectXError(kUploadCreationFailed, result,
				"Failed to create index upload staging buffer."));
		}

		void* mapped = nullptr;
		const D3D12_RANGE readRange { 0, 0 };
		result = uploadResource->Map(0, &readRange, &mapped);
		if(FAILED(result)) {
			return std::unexpected(MakeDirectXError(kUploadMapFailed, result,
				"Failed to map index upload staging buffer."));
		}
		std::memcpy(mapped, data.data(), data.size());
		uploadResource->Unmap(0, nullptr);

		ImmediateUploadContext uploadContext;
		auto contextResult = uploadContext.Initialize(device);
		if(!contextResult) return contextResult;
		auto* commandList = uploadContext.GetCommandList();
		commandList->CopyBufferRegion(destination.Get(), 0, uploadResource.Get(), 0, data.size());
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = destination.Get();
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_INDEX_BUFFER;
		commandList->ResourceBarrier(1, &barrier);

		// stagingとCopy commandはGPUが参照を終えるまでscope内に保持する。
		auto executeResult = uploadContext.ExecuteAndWait(commandQueue);
		if(!executeResult) return executeResult;
		indexBuffer.Commit(
			destination.Get(), static_cast<uint32_t>(data.size()), format,
			static_cast<uint32_t>(data.size() / stride));
		return {};
	}
} // namespace NexusEngine
