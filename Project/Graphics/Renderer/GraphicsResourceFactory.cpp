#include "GraphicsResourceFactory.h"

// c++
#include <cmath>
#include <limits>
#include <memory>
#include <utility>

// directx
#include <d3d12.h>

// engine
#include "Graphics/Core/CommandQueue.h"
#include "Graphics/Descriptor/BindlessDescriptorTable.h"
#include "Graphics/Resource/ConstantBuffer.h"
#include "Graphics/Resource/IndexBuffer.h"
#include "Graphics/Resource/IndexBufferUploader.h"
#include "Graphics/Resource/MeshResource.h"
#include "Graphics/Model/ModelAssetData.h"
#include "Graphics/Model/ModelResource.h"
#include "Graphics/Resource/SamplerDesc.h"
#include "Graphics/Resource/TextureFormatDx12.h"
#include "Graphics/Resource/TextureResource.h"
#include "Graphics/Resource/TextureUploader.h"
#include "Graphics/Resource/VertexBuffer.h"
#include "Graphics/Resource/VertexBufferUploader.h"
#include "Graphics/Resource/WicImageDecoder.h"
#include "Graphics/Shader/Shader.h"

namespace NexusEngine {
	namespace {
		constexpr int32_t kInvalidResourceArgument = 1;

		template<typename Keyframe>
		[[nodiscard]] bool HasValidKeyframeTimes(
			const std::vector<Keyframe>& keyframes,
			const float duration) noexcept {
			float previousTime = -1.0f;
			for(const Keyframe& keyframe : keyframes) {
				if(!std::isfinite(keyframe.time) || keyframe.time < 0.0f ||
				   keyframe.time > duration || keyframe.time <= previousTime) {
					return false;
				}
				previousTime = keyframe.time;
			}
			return true;
		}
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
		const GraphicsPipelineDesc& pipelineDesc) const {
		// Deviceを必要とする既存DX12実装はFactoryの背後へ留め、Rendererの初期化契約を安定させる。
		return pipeline.Initialize(
			static_cast<ID3D12Device*>(nativeDevice_),
			std::move(vertexShader), std::move(pixelShader), pipelineDesc);
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

	Result<void> GraphicsResourceFactory::CreateIndexBuffer(
		IndexBuffer& indexBuffer,
		const std::span<const uint8_t> data,
		const IndexFormat format) const {
		// Vertex Bufferと同じImmediate Upload経路を使い、RendererからNative Deviceを隠蔽する。
		const IndexBufferUploader uploader;
		return uploader.Upload(
			static_cast<ID3D12Device*>(nativeDevice_), commandQueue_, indexBuffer, data, format);
	}

	Result<void> GraphicsResourceFactory::CreateMeshResource(
		MeshResource& mesh,
		const std::span<const uint8_t> vertexData,
		const uint32_t vertexStride,
		const std::span<const uint8_t> indexData,
		const IndexFormat indexFormat,
		const std::vector<SubmeshRange>& submeshes) const {
		if(mesh.IsInitialized() || submeshes.empty()) {
			return std::unexpected(Error(ErrorCategory::Graphics, kInvalidResourceArgument,
				"Mesh resource state or submesh list is invalid."));
		}

		const uint32_t indexCount = static_cast<uint32_t>(
			indexData.size() / (indexFormat == IndexFormat::UInt32 ? sizeof(uint32_t) : sizeof(uint16_t)));
		for(const SubmeshRange& submesh : submeshes) {
			const uint64_t rangeEnd = static_cast<uint64_t>(submesh.firstIndex) + submesh.indexCount;
			if(submesh.indexCount == 0 || rangeEnd > indexCount) {
				return std::unexpected(Error(ErrorCategory::Graphics, kInvalidResourceArgument,
					"Submesh index range is outside the supplied index buffer."));
			}
		}

		// Meshは両Bufferが揃ったときだけ有効。Index upload失敗時はVertex Resourceを即座破棄する。
		auto result = CreateVertexBuffer(mesh.vertexBuffer_, vertexData, vertexStride);
		if(!result) return result;
		result = CreateIndexBuffer(mesh.indexBuffer_, indexData, indexFormat);
		if(!result) {
			mesh.vertexBuffer_.Shutdown();
			return result;
		}
		mesh.submeshes_ = submeshes;
		return {};
	}

	Result<void> GraphicsResourceFactory::CreateModelResource(
		ModelResource& model,
		const ModelAssetData& assetData) const {
		if(model.IsInitialized() || assetData.meshes.empty()) {
			return std::unexpected(Error(ErrorCategory::Graphics, kInvalidResourceArgument,
				"Model resource state or asset data is invalid."));
		}
		const uint32_t materialCount = assetData.materials.empty()
			? 1U : static_cast<uint32_t>(assetData.materials.size());
		for(const MeshAssetData& mesh : assetData.meshes) {
			for(const SubmeshRange& submesh : mesh.submeshes) {
				if(submesh.materialSlot >= materialCount) {
					return std::unexpected(Error(ErrorCategory::Graphics, kInvalidResourceArgument,
						"Submesh references an invalid model material slot."));
				}
			}
		}

		// hierarchyが参照するMesh/Parent indexをGPU upload前に検証し、部分生成を避ける。
		for(uint32_t nodeIndex = 0; nodeIndex < assetData.nodes.size(); ++nodeIndex) {
			const ModelNodeAssetData& node = assetData.nodes[nodeIndex];
			if(node.parentIndex != ModelNodeAssetData::kNoParent && node.parentIndex >= nodeIndex) {
				return std::unexpected(Error(ErrorCategory::Graphics, kInvalidResourceArgument,
					"Model node parent must precede its child in hierarchy order."));
			}
			for(const uint32_t meshIndex : node.meshIndices) {
				if(meshIndex >= assetData.meshes.size()) {
					return std::unexpected(Error(ErrorCategory::Graphics, kInvalidResourceArgument,
						"Model node references an invalid mesh index."));
				}
			}
			if(!node.meshIndices.empty()) {
				const uint32_t nodeSkin = assetData.meshes[node.meshIndices.front()].skinIndex;
				for(const uint32_t meshIndex : node.meshIndices) {
					if(assetData.meshes[meshIndex].skinIndex != nodeSkin) {
						return std::unexpected(Error(ErrorCategory::Graphics, kInvalidResourceArgument,
							"All meshes referenced by one model node must use the same skin."));
					}
				}
			}
		}
		for(const AnimationClip& clip : assetData.animations) {
			if(!std::isfinite(clip.duration) || clip.duration < 0.0f) {
				return std::unexpected(Error(ErrorCategory::Graphics, kInvalidResourceArgument,
					"Model animation duration cannot be negative."));
			}
			for(const NodeAnimationChannel& channel : clip.channels) {
				if(channel.nodeIndex >= assetData.nodes.size()) {
					return std::unexpected(Error(ErrorCategory::Graphics, kInvalidResourceArgument,
						"Model animation channel references an invalid node."));
				}
				if(!HasValidKeyframeTimes(channel.translations, clip.duration) ||
				   !HasValidKeyframeTimes(channel.rotations, clip.duration) ||
				   !HasValidKeyframeTimes(channel.scales, clip.duration)) {
					return std::unexpected(Error(ErrorCategory::Graphics, kInvalidResourceArgument,
						"Model animation keyframe times must be strictly increasing and inside the clip."));
				}
			}
		}
		for(const ModelSkin& skin : assetData.skins) {
			if(skin.joints.empty() || skin.joints.size() > kMaxSkinJoints ||
			   skin.joints.size() != skin.inverseBindMatrices.size()) {
				return std::unexpected(Error(ErrorCategory::Graphics, kInvalidResourceArgument,
					"Model skin joint and inverse bind matrix counts are invalid."));
			}
			for(const uint32_t nodeIndex : skin.joints) {
				if(nodeIndex >= assetData.nodes.size()) {
					return std::unexpected(Error(ErrorCategory::Graphics, kInvalidResourceArgument,
						"Model skin references an invalid joint node."));
				}
			}
		}
		for(const MeshAssetData& mesh : assetData.meshes) {
			if(mesh.skinIndex != MeshAssetData::kNoSkin && mesh.skinIndex >= assetData.skins.size()) {
				return std::unexpected(Error(ErrorCategory::Graphics, kInvalidResourceArgument,
					"Model mesh references an invalid skin."));
			}
		}

		std::vector<std::unique_ptr<MeshResource>> meshes;
		meshes.reserve(assetData.meshes.size());
		for(const MeshAssetData& source : assetData.meshes) {
			auto mesh = std::make_unique<MeshResource>();
			auto result = CreateMeshResource(
				*mesh, source.vertexData, source.vertexStride,
				source.indexData, source.indexFormat, source.submeshes);
			if(!result) return result;
			meshes.push_back(std::move(mesh));
		}

		std::vector<ModelNode> nodes;
		nodes.reserve(assetData.nodes.size());
		for(const ModelNodeAssetData& source : assetData.nodes) {
			nodes.push_back({ source.name, source.parentIndex, source.localTransform,
				MakeAffineMatrix(source.localTransform.scale, source.localTransform.rotation,
					source.localTransform.translation), source.meshIndices });
		}
		// Nodeを持たない単一Mesh Assetも扱えるよう、暗黙のroot nodeを補う。
		if(nodes.empty()) {
			ModelNode root;
			root.name = "Root";
			root.meshIndices.reserve(meshes.size());
			for(uint32_t index = 0; index < meshes.size(); ++index) root.meshIndices.push_back(index);
			nodes.push_back(std::move(root));
		}

		model.meshes_ = std::move(meshes);
		model.nodes_ = std::move(nodes);
		model.animations_ = assetData.animations;
		model.skins_ = assetData.skins;
		model.meshSkinIndices_.reserve(assetData.meshes.size());
		for(const MeshAssetData& mesh : assetData.meshes) model.meshSkinIndices_.push_back(mesh.skinIndex);
		if(assetData.materials.empty()) {
			model.materials_.push_back({ "Default" });
		} else {
			model.materials_.reserve(assetData.materials.size());
			for(const ModelMaterialAssetData& material : assetData.materials) {
				model.materials_.push_back({ material.name, material.baseColorFactor, material.sampler });
			}
		}
		return {};
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

	Result<void> GraphicsResourceFactory::CreateTexture2DFromEncodedData(
		TextureResource& texture,
		const std::span<const uint8_t> encodedData) const {
		const WicImageDecoder decoder;
		auto image = decoder.Decode(encodedData);
		if(!image) return std::unexpected(std::move(image.error()));
		return CreateTexture2D(texture,
			TextureDesc { image->width, image->height, TextureFormat::Rgba8UnormSrgb },
			image->rgbaPixels);
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
