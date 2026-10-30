#include "TextureUploader.h"

// c++
#include <cstring>
#include <limits>
#include <memory>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include <chrono>
#include "Foundation/Logging/Logger.h"

// directx
#include <d3d12.h>
#include <wrl/client.h>

// engine
#include "Graphics/Core/CommandQueue.h"
#include "ImmediateUploadContext.h"
#include "TextureFormatDx12.h"
#include "Graphics/Color/ColorSpace.h"

namespace NexusEngine {
	namespace {
		constexpr int32_t  kInvalidArgument		  = 1;
		constexpr int32_t  kTextureCreationFailed = 2;
		constexpr int32_t  kUploadCreationFailed  = 3;
		constexpr int32_t  kUploadMapFailed		  = 4;

		// Area filtering includes the entire image, including odd-sized edges and 1D tails.
		void GenerateMip(uint8_t* mapped, const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& source,
			const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& target, TextureFormat format) {
			const bool hdr = format == TextureFormat::Rgba32Float;
			const bool srgb = format == TextureFormat::Rgba8UnormSrgb;
			const size_t pixelBytes = hdr ? 16 : 4;
			// 入力は8bitなので、各画素のpow計算を256要素の正確な変換表で置き換える。
			static const auto linearColors = [] {
				std::array<float, 256> values{};
				for(size_t i = 0; i < values.size(); ++i) values[i] = ColorSpace::SrgbToLinear(float(i) / 255.0f);
				return values;
			}();
			for(UINT y = 0; y < target.Footprint.Height; ++y) {
				const double y0 = double(y) * source.Footprint.Height / target.Footprint.Height;
				const double y1 = double(y + 1) * source.Footprint.Height / target.Footprint.Height;
				for(UINT x = 0; x < target.Footprint.Width; ++x) {
					const double x0 = double(x) * source.Footprint.Width / target.Footprint.Width;
					const double x1 = double(x + 1) * source.Footprint.Width / target.Footprint.Width;
					std::array<double, 4> sum {};
					for(UINT sy = static_cast<UINT>(y0); sy < static_cast<UINT>(std::ceil(y1)); ++sy) {
						for(UINT sx = static_cast<UINT>(x0); sx < static_cast<UINT>(std::ceil(x1)); ++sx) {
							const double weight = ((std::min)(x1, double(sx + 1)) - (std::max)(x0, double(sx))) *
								((std::min)(y1, double(sy + 1)) - (std::max)(y0, double(sy)));
							const auto* pixel = mapped + source.Offset + size_t(sy) * source.Footprint.RowPitch + size_t(sx) * pixelBytes;
							std::array<float, 4> value;
							if(hdr) std::memcpy(value.data(), pixel, 16);
							else for(size_t c = 0; c < 4; ++c) value[c] = float(pixel[c]) / 255.0f;
							if(srgb) for(size_t c = 0; c < 3; ++c) value[c] = linearColors[pixel[c]];
							for(size_t c = 0; c < 4; ++c) sum[c] += value[c] * weight;
						}
					}
					std::array<float, 4> value;
					for(size_t c = 0; c < 4; ++c) value[c] = static_cast<float>(sum[c] / ((x1 - x0) * (y1 - y0)));
					if(srgb) for(size_t c = 0; c < 3; ++c) value[c] = ColorSpace::LinearToSrgb(value[c]);
					auto* pixel = mapped + target.Offset + size_t(y) * target.Footprint.RowPitch + size_t(x) * pixelBytes;
					if(hdr) std::memcpy(pixel, value.data(), 16);
					else for(size_t c = 0; c < 4; ++c) pixel[c] = static_cast<uint8_t>(std::clamp(value[c], 0.0f, 1.0f) * 255.0f + 0.5f);
				}
			}
		}

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
		const auto uploadStart = std::chrono::steady_clock::now();
		const uint32_t kBytesPerPixel = desc.format == TextureFormat::Rgba32Float ? 16 : 4;
		if(ToNativeTextureFormat(desc.format) == DXGI_FORMAT_UNKNOWN)
			return std::unexpected(Error(ErrorCategory::Graphics, kInvalidArgument, "Unsupported texture format."));
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
		if(desc.generateMipmaps) {
			for(uint32_t size = (std::max)(desc.width, desc.height); size > 1; size >>= 1)
				++textureDesc.MipLevels;
		}
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
		std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(textureDesc.MipLevels);
		std::vector<UINT> rowCounts(textureDesc.MipLevels);
		std::vector<UINT64> rowSizes(textureDesc.MipLevels);
		UINT64							   uploadSize = 0;
		device->GetCopyableFootprints(&textureDesc, 0, textureDesc.MipLevels, 0,
			footprints.data(), rowCounts.data(), rowSizes.data(), &uploadSize);
		if(uploadSize == 0 || uploadSize > (std::numeric_limits<size_t>::max)() || rowCounts[0] != desc.height || rowSizes[0] != sourceRowSize) {
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

		// Generate in ordinary CPU memory: upload heaps may be write-combined and slow to read.
		std::vector<uint8_t> staging(static_cast<size_t>(uploadSize));
		// Sourceは密配置、GPU footprintは256byte row pitchを持ち得るため、row単位でpaddingを飛ばしてコピーする。
		for(uint32_t row = 0; row < desc.height; ++row) {
			std::memcpy(
				staging.data() + footprints[0].Offset + static_cast<size_t>(row) * footprints[0].Footprint.RowPitch,
				pixels.data() + static_cast<size_t>(row) * sourceRowSize,
				sourceRowSize);
		}
		const auto mipStart = std::chrono::steady_clock::now();
		for(size_t mip = 1; mip < footprints.size(); ++mip)
			GenerateMip(staging.data(), footprints[mip - 1], footprints[mip], desc.format);
		const auto mipEnd = std::chrono::steady_clock::now();
		uint8_t* mapped = nullptr;
		const D3D12_RANGE readRange{0, 0};
		result = uploadResource->Map(0, &readRange, reinterpret_cast<void**>(&mapped));
		if(FAILED(result)) {
			return std::unexpected(MakeDirectXError(kUploadMapFailed, result, "Failed to map texture upload staging resource."));
		}
		std::memcpy(mapped, staging.data(), staging.size());
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
		for(UINT mip = 0; mip < textureDesc.MipLevels; ++mip) {
			destinationLocation.SubresourceIndex = mip;
			sourceLocation.PlacedFootprint = footprints[mip];
			commandList->CopyTextureRegion(&destinationLocation, 0, 0, 0, &sourceLocation, nullptr);
		}

		// Upload完了後の標準利用状態へ遷移し、Renderer側にResource Barrier責務を漏らさない。
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type				   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource   = destination.Get();
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		commandList->ResourceBarrier(1, &barrier);
		// Textureとstagingは共通ContextのFence待機完了までscope内で保持する。
		const auto waitStart = std::chrono::steady_clock::now();
		auto executeResult = uploadContext.ExecuteAndWait(commandQueue);
		if(!executeResult) return executeResult;

		// GPU copy完了後だけTextureResourceへ所有権をcommitし、失敗したuploadを公開しない。
		texture.Commit(destination.Get(), desc);
		const auto end = std::chrono::steady_clock::now();
		const double totalMs = std::chrono::duration<double, std::milli>(end - uploadStart).count();
		// 遅い画像だけを記録し、ミップ生成とGPU転送待ちを区別する。
		if(totalMs >= 25) NEXUS_LOG_INFO("Assets.Profile", "Texture " + std::to_string(desc.width) + "x" + std::to_string(desc.height) +
			", mips=" + std::to_string(textureDesc.MipLevels) + ", mipCPU=" + std::to_string(std::chrono::duration<double, std::milli>(mipEnd - mipStart).count()) +
			", submitWait=" + std::to_string(std::chrono::duration<double, std::milli>(end - waitStart).count()) + ", total=" + std::to_string(totalMs) + " ms.");
		return {};
	}
} // namespace NexusEngine
