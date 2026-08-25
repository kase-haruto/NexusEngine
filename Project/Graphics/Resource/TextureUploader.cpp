#include "TextureUploader.h"

// c++
#include <cstring>
#include <limits>
#include <memory>

// directx
#include <d3d12.h>
#include <wrl/client.h>

// engine
#include "Graphics/Core/CommandQueue.h"
#include "ImmediateUploadContext.h"
#include "TextureFormatDx12.h"

namespace NexusEngine {
	namespace {
		constexpr int32_t  kInvalidArgument		  = 1;
		constexpr int32_t  kTextureCreationFailed = 2;
		constexpr int32_t  kUploadCreationFailed  = 3;
		constexpr int32_t  kUploadMapFailed		  = 4;
		constexpr uint32_t kBytesPerPixel		  = 4;

	} // namespace

	/////////////////////////////////////////////////////////////////////////////////////////
	// CPU RGBA pixelをDefault Heap Textureへ転送してShader Resource状態へ確定する
	/////////////////////////////////////////////////////////////////////////////////////////
	Result<void> TextureUploader::Upload(
		ID3D12Device* const			   device,
		CommandQueue* const			   commandQueue,
		TextureResource&			   texture,
		const TextureDesc&			   desc,
		const std::span<const uint8_t> pixels) const {
		if(device == nullptr || commandQueue == nullptr || texture.IsInitialized() || desc.width == 0 || desc.height == 0 ||
		   desc.width > (std::numeric_limits<size_t>::max)() / kBytesPerPixel) {
			return std::unexpected(Error(ErrorCategory::Graphics, kInvalidArgument, "Texture upload arguments or state are invalid."));
		}

		const size_t sourceRowSize = static_cast<size_t>(desc.width) * kBytesPerPixel;
		if(desc.height > (std::numeric_limits<size_t>::max)() / sourceRowSize ||
		   pixels.size() != sourceRowSize * desc.height) {
			return std::unexpected(Error(ErrorCategory::Graphics, kInvalidArgument, "Texture pixel data size does not match its dimensions."));
		}

		D3D12_RESOURCE_DESC textureDesc = {};
		textureDesc.Dimension			= D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		textureDesc.Width				= desc.width;
		textureDesc.Height				= desc.height;
		textureDesc.DepthOrArraySize	= 1;
		textureDesc.MipLevels			= 1;
		textureDesc.Format				= ToNativeTextureFormat(desc.format);
		textureDesc.SampleDesc.Count	= 1;
		textureDesc.Layout				= D3D12_TEXTURE_LAYOUT_UNKNOWN;

		// Default HeapはCPUから直接Mapできないため、COPY_DESTで生成してstaging経由で転送する。
		D3D12_HEAP_PROPERTIES defaultHeap = {};
		defaultHeap.Type				  = D3D12_HEAP_TYPE_DEFAULT;
		Microsoft::WRL::ComPtr<ID3D12Resource> destination;
		HRESULT								   result = device->CreateCommittedResource(
			&defaultHeap, D3D12_HEAP_FLAG_NONE, &textureDesc,
			D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&destination));
		if(FAILED(result)) {
			return std::unexpected(MakeDirectXError(kTextureCreationFailed, result, "Failed to create default heap texture."));
		}

		// Driverが要求するrow pitchと配置alignmentを取得し、手計算によるGPU copy規約違反を避ける。
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint  = {};
		UINT							   rowCount	  = 0;
		UINT64							   rowSize	  = 0;
		UINT64							   uploadSize = 0;
		device->GetCopyableFootprints(&textureDesc, 0, 1, 0, &footprint, &rowCount, &rowSize, &uploadSize);
		if(uploadSize == 0 || uploadSize > (std::numeric_limits<size_t>::max)() || rowCount != desc.height || rowSize != sourceRowSize) {
			return std::unexpected(Error(ErrorCategory::Graphics, kInvalidArgument, "Texture copy footprint is invalid or unsupported."));
		}

		D3D12_HEAP_PROPERTIES uploadHeap = {};
		uploadHeap.Type					 = D3D12_HEAP_TYPE_UPLOAD;
		D3D12_RESOURCE_DESC uploadDesc	 = {};
		uploadDesc.Dimension			 = D3D12_RESOURCE_DIMENSION_BUFFER;
		uploadDesc.Width				 = uploadSize;
		uploadDesc.Height				 = 1;
		uploadDesc.DepthOrArraySize		 = 1;
		uploadDesc.MipLevels			 = 1;
		uploadDesc.SampleDesc.Count		 = 1;
		uploadDesc.Layout				 = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		Microsoft::WRL::ComPtr<ID3D12Resource> uploadResource;
		result = device->CreateCommittedResource(
			&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadResource));
		if(FAILED(result)) {
			return std::unexpected(MakeDirectXError(kUploadCreationFailed, result, "Failed to create texture upload staging resource."));
		}

		uint8_t*		  mapped = nullptr;
		const D3D12_RANGE readRange{0, 0};
		result = uploadResource->Map(0, &readRange, reinterpret_cast<void**>(&mapped));
		if(FAILED(result)) {
			return std::unexpected(MakeDirectXError(kUploadMapFailed, result, "Failed to map texture upload staging resource."));
		}
		// Sourceは密配置、GPU footprintは256byte row pitchを持ち得るため、row単位でpaddingを飛ばしてコピーする。
		for(uint32_t row = 0; row < desc.height; ++row) {
			std::memcpy(
				mapped + footprint.Offset + static_cast<size_t>(row) * footprint.Footprint.RowPitch,
				pixels.data() + static_cast<size_t>(row) * sourceRowSize,
				sourceRowSize);
		}
		uploadResource->Unmap(0, nullptr);

		ImmediateUploadContext uploadContext;
		auto				   contextResult = uploadContext.Initialize(device);
		if(!contextResult) return contextResult;
		auto* const commandList = uploadContext.GetCommandList();

		D3D12_TEXTURE_COPY_LOCATION destinationLocation = {};
		destinationLocation.pResource					= destination.Get();
		destinationLocation.Type						= D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		D3D12_TEXTURE_COPY_LOCATION sourceLocation		= {};
		sourceLocation.pResource						= uploadResource.Get();
		sourceLocation.Type								= D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		sourceLocation.PlacedFootprint					= footprint;
		commandList->CopyTextureRegion(&destinationLocation, 0, 0, 0, &sourceLocation, nullptr);

		// Upload完了後の標準利用状態へ遷移し、Renderer側にResource Barrier責務を漏らさない。
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type				   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource   = destination.Get();
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		commandList->ResourceBarrier(1, &barrier);
		// Textureとstagingは共通ContextのFence待機完了までscope内で保持する。
		auto executeResult = uploadContext.ExecuteAndWait(commandQueue);
		if(!executeResult) return executeResult;

		// GPU copy完了後だけTextureResourceへ所有権をcommitし、失敗したuploadを公開しない。
		texture.Commit(destination.Get(), desc);
		return {};
	}
} // namespace NexusEngine
