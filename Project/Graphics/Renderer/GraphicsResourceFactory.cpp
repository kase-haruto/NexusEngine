#include "GraphicsResourceFactory.h"

// c++
#include <cmath>
#include <limits>
#include <utility>

// directx
#include <d3d12.h>

// engine
#include "Graphics/Core/CommandQueue.h"
#include "Graphics/Descriptor/BindlessDescriptorTable.h"
#include "Graphics/Resource/ConstantBuffer.h"
#include "Graphics/Resource/SamplerDesc.h"
#include "Graphics/Resource/TextureFormatDx12.h"
#include "Graphics/Resource/TextureResource.h"
#include "Graphics/Resource/TextureUploader.h"
#include "Graphics/Resource/VertexBuffer.h"
#include "Graphics/Resource/VertexBufferUploader.h"
#include "Graphics/Shader/Shader.h"

namespace NexusEngine {
	namespace {
		constexpr int32_t kInvalidResourceArgument = 1;
	}

	void GraphicsResourceFactory::ConnectBackend(
		void* const					   nativeDevice,
		CommandQueue* const			   commandQueue,
		BindlessDescriptorTable* const bindlessDescriptors,
		const uint32_t				   framesInFlight) noexcept {
		// 接続と切断はGraphicsSystemだけが行い、FactoryがDevice寿命を越えて利用されることを防ぐ。
		nativeDevice_		 = nativeDevice;
		commandQueue_		 = commandQueue;
		bindlessDescriptors_ = bindlessDescriptors;
		framesInFlight_		 = framesInFlight;
	}

	Result<void> GraphicsResourceFactory::CreateGraphicsPipeline(
		GraphicsPipeline&					pipeline,
		Shader								vertexShader,
		Shader								pixelShader,
		const std::vector<VertexAttribute>& vertexLayout) const {
		// Deviceを必要とする既存DX12実装はFactoryの背後へ留め、Rendererの初期化契約を安定させる。
		return pipeline.Initialize(
			static_cast<ID3D12Device*>(nativeDevice_),
			std::move(vertexShader), std::move(pixelShader), vertexLayout);
	}

	Result<void> GraphicsResourceFactory::CreateVertexBuffer(
		VertexBuffer&				   vertexBuffer,
		const std::span<const uint8_t> data,
		const uint32_t				   stride) const {
		// 静的GeometryはUpload Heapへ常駐させず、専用UploaderからDefault Heapへ転送する。
		const VertexBufferUploader uploader;
		return uploader.Upload(
			static_cast<ID3D12Device*>(nativeDevice_), commandQueue_, vertexBuffer, data, stride);
	}

	Result<void> GraphicsResourceFactory::CreateConstantBuffer(
		ConstantBuffer& constantBuffer,
		const size_t	dataSize) const {
		// Frame数はSwapChain/FrameContext所有者が決定し、Renderer側に固定値を重複させない。
		return constantBuffer.Initialize(nativeDevice_, dataSize, framesInFlight_);
	}

	Result<std::vector<ShaderResourceRef>> GraphicsResourceFactory::CreatePersistentConstantBufferShaderResources(
		const ConstantBuffer& constantBuffer) const {
		if(bindlessDescriptors_ == nullptr || constantBuffer.GetFrameCount() == 0 ||
		   constantBuffer.GetAlignedSliceSize() == 0 ||
		   constantBuffer.GetAlignedSliceSize() > (std::numeric_limits<UINT>::max)()) {
			return std::unexpected(Error(ErrorCategory::Graphics, kInvalidResourceArgument, "Constant buffer shader resource arguments are invalid."));
		}

		std::vector<ShaderResourceRef> references;
		references.reserve(constantBuffer.GetFrameCount());
		for(uint32_t frameIndex = 0; frameIndex < constantBuffer.GetFrameCount(); ++frameIndex) {
			D3D12_CONSTANT_BUFFER_VIEW_DESC desc = {};
			desc.BufferLocation					 = constantBuffer.GetGpuAddress(frameIndex);
			desc.SizeInBytes					 = static_cast<UINT>(constantBuffer.GetAlignedSliceSize());
			auto reference						 = bindlessDescriptors_->CreateConstantBufferView(desc);
			if(!reference) {
				// 生成済みslotも再利用可能に戻し、部分的なDescriptor所有を呼び出し側へ返さない。
				for(const auto createdReference : references) {
					static_cast<void>(RetirePersistentShaderResource(createdReference));
				}
				return std::unexpected(std::move(reference.error()));
			}
			references.push_back(*reference);
		}
		return references;
	}

	Result<void> GraphicsResourceFactory::CreateTexture2D(
		TextureResource&			   texture,
		const TextureDesc&			   desc,
		const std::span<const uint8_t> pixels) const {
		// UploadのCommand資源とFence待機は専用サービスへ閉じ、FactoryはBackend接続だけを提供する。
		const TextureUploader uploader;
		return uploader.Upload(
			static_cast<ID3D12Device*>(nativeDevice_), commandQueue_, texture, desc, pixels);
	}

	Result<ShaderResourceRef> GraphicsResourceFactory::CreatePersistentTextureShaderResource(
		const TextureResource& texture) const {
		if(bindlessDescriptors_ == nullptr || !texture.IsInitialized()) {
			return std::unexpected(Error(ErrorCategory::Graphics, kInvalidResourceArgument, "Texture shader resource arguments or backend state are invalid."));
		}

		D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
		desc.Format							 = ToNativeTextureFormat(texture.GetFormat());
		desc.ViewDimension					 = D3D12_SRV_DIMENSION_TEXTURE2D;
		desc.Shader4ComponentMapping		 = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		desc.Texture2D.MostDetailedMip		 = 0;
		desc.Texture2D.MipLevels			 = 1;
		return bindlessDescriptors_->CreateShaderResourceView(
			static_cast<ID3D12Resource*>(texture.GetNativeResource()), desc);
	}

	Result<ShaderResourceRef> GraphicsResourceFactory::CreatePersistentSampler(const SamplerDesc& desc) const {
		if(bindlessDescriptors_ == nullptr || !std::isfinite(desc.mipLodBias) ||
		   !std::isfinite(desc.minLod) || !std::isfinite(desc.maxLod) ||
		   desc.minLod < 0.0f || desc.maxLod < desc.minLod) {
			return std::unexpected(Error(ErrorCategory::Graphics, kInvalidResourceArgument, "Sampler description or backend state is invalid."));
		}

		auto toNativeAddressMode = [](const TextureAddressMode mode) noexcept {
			switch(mode) {
			case TextureAddressMode::Mirror:
				return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
			case TextureAddressMode::Clamp:
				return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			case TextureAddressMode::Repeat:
			default:
				return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			}
		};

		D3D12_SAMPLER_DESC nativeDesc = {};
		nativeDesc.Filter			  = desc.filter == TextureFilter::Nearest
											? D3D12_FILTER_MIN_MAG_MIP_POINT
											: D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		nativeDesc.AddressU			  = toNativeAddressMode(desc.addressU);
		nativeDesc.AddressV			  = toNativeAddressMode(desc.addressV);
		nativeDesc.AddressW			  = toNativeAddressMode(desc.addressW);
		nativeDesc.MipLODBias		  = desc.mipLodBias;
		nativeDesc.MinLOD			  = desc.minLod;
		nativeDesc.MaxLOD			  = desc.maxLod;
		nativeDesc.MaxAnisotropy	  = 1;
		return bindlessDescriptors_->CreateSampler(nativeDesc);
	}

	Result<void> GraphicsResourceFactory::RetirePersistentShaderResource(const ShaderResourceRef reference) const {
		if(bindlessDescriptors_ == nullptr || commandQueue_ == nullptr || !bindlessDescriptors_->IsValid(reference)) {
			return std::unexpected(Error(ErrorCategory::Graphics, kInvalidResourceArgument, "Shader resource reference or backend state is invalid."));
		}

		// Queue末尾へFenceを置くことで、この参照を使う可能性がある既投入Command全体を包含する。
		auto fenceResult = commandQueue_->Signal();
		if(!fenceResult) {
			return std::unexpected(std::move(fenceResult.error()));
		}
		return bindlessDescriptors_->Retire(reference, *fenceResult);
	}

	bool GraphicsResourceFactory::IsPersistentShaderResourceValid(const ShaderResourceRef reference) const noexcept {
		return bindlessDescriptors_ != nullptr && bindlessDescriptors_->IsValid(reference);
	}
} // namespace NexusEngine
