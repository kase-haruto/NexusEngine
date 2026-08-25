#include "VertexBufferUploader.h"
#include "ImmediateUploadContext.h"
#include "VertexBuffer.h"
#include <cstring>
#include <d3d12.h>
#include <limits>
#include <wrl/client.h>

namespace NexusEngine {
	namespace {
		constexpr int32_t kInvalidArgument = 1, kDestinationCreationFailed = 2, kUploadCreationFailed = 3, kUploadMapFailed = 4;
	}
	Result<void> VertexBufferUploader::Upload(ID3D12Device* const device, CommandQueue* const commandQueue, VertexBuffer& vertexBuffer, const std::span<const uint8_t> data, const uint32_t stride) const {
		if(device == nullptr || commandQueue == nullptr || vertexBuffer.IsInitialized() || data.empty() || stride == 0 || data.size() % stride != 0 || data.size() > (std::numeric_limits<UINT>::max)() || data.size() / stride > (std::numeric_limits<uint32_t>::max)())
			return std::unexpected(Error(ErrorCategory::Graphics, kInvalidArgument, "Vertex buffer upload arguments or state are invalid."));
		D3D12_RESOURCE_DESC bufferDesc	  = {};
		bufferDesc.Dimension			  = D3D12_RESOURCE_DIMENSION_BUFFER;
		bufferDesc.Width				  = data.size();
		bufferDesc.Height				  = 1;
		bufferDesc.DepthOrArraySize		  = 1;
		bufferDesc.MipLevels			  = 1;
		bufferDesc.SampleDesc.Count		  = 1;
		bufferDesc.Layout				  = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		D3D12_HEAP_PROPERTIES defaultHeap = {};
		defaultHeap.Type				  = D3D12_HEAP_TYPE_DEFAULT;
		Microsoft::WRL::ComPtr<ID3D12Resource> destination;
		HRESULT								   result = device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&destination));
		if(FAILED(result)) return std::unexpected(MakeDirectXError(kDestinationCreationFailed, result, "Failed to create default heap vertex buffer."));
		D3D12_HEAP_PROPERTIES uploadHeap = {};
		uploadHeap.Type					 = D3D12_HEAP_TYPE_UPLOAD;
		Microsoft::WRL::ComPtr<ID3D12Resource> uploadResource;
		result = device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadResource));
		if(FAILED(result)) return std::unexpected(MakeDirectXError(kUploadCreationFailed, result, "Failed to create vertex upload staging buffer."));
		void*			  mapped = nullptr;
		const D3D12_RANGE readRange{0, 0};
		result = uploadResource->Map(0, &readRange, &mapped);
		if(FAILED(result)) return std::unexpected(MakeDirectXError(kUploadMapFailed, result, "Failed to map vertex upload staging buffer."));
		std::memcpy(mapped, data.data(), data.size());
		uploadResource->Unmap(0, nullptr);
		ImmediateUploadContext uploadContext;
		auto				   contextResult = uploadContext.Initialize(device);
		if(!contextResult) return contextResult;
		auto* commandList = uploadContext.GetCommandList();
		commandList->CopyBufferRegion(destination.Get(), 0, uploadResource.Get(), 0, data.size());
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type				   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource   = destination.Get();
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
		commandList->ResourceBarrier(1, &barrier);
		// stagingと転送先はFence完了までscope内で保持し、GPU参照中の破棄を防ぐ。
		auto executeResult = uploadContext.ExecuteAndWait(commandQueue);
		if(!executeResult) return executeResult;
		vertexBuffer.Commit(destination.Get(), static_cast<uint32_t>(data.size()), stride, static_cast<uint32_t>(data.size() / stride));
		return {};
	}
} // namespace NexusEngine
