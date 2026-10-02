#include "PrimitiveRenderer.h"

// c++
#include <array>
#include <cstddef>
#include <span>
#include <utility>

// engine
#include "GraphicsContext.h"
#include "GraphicsResourceFactory.h"
#include "Graphics/Resource/SamplerDesc.h"
#include "Graphics/Model/ModelAssetData.h"

#include "Foundation/Logging/Logger.h"

namespace NexusEngine {
	/////////////////////////////////////////////////////////////////////////////////////////
	// Primitive描画に必要なShader、Pipeline、Vertex/Index Bufferを初期化する
	/////////////////////////////////////////////////////////////////////////////////////////
	Result<void> PrimitiveRenderer::Initialize(
		const GraphicsRendererInitializationContext& context) {
		resources_ = &context.resources;
		// Shaderはこの初期化時にCompileとReflectionを一度だけ行い、Draw中には解析しない。
		Shader vertexShader;
		auto result = vertexShader.Initialize(
			{ context.shaderDirectory / L"Primitive/Primitive.VS.hlsl", L"main", L"vs_6_6" },
			ShaderStage::Vertex);
		if(!result) {
			return std::unexpected(std::move(result.error()));
		}

		Shader pixelShader;
		result = pixelShader.Initialize(
			{ context.shaderDirectory / L"Primitive/Primitive.PS.hlsl", L"main", L"ps_6_6" },
			ShaderStage::Pixel);
		if(!result) {
			return std::unexpected(std::move(result.error()));
		}

		// Reflectionでsemanticを検証しつつ、CPU構造からしか安全に決められないoffsetを明示する。
		GraphicsPipelineDesc pipelineDesc;
		pipelineDesc.vertexLayout = {
			{ "POSITION", 0, VertexFormat::Float3, offsetof(ModelVertex, position) },
			{ "NORMAL", 0, VertexFormat::Float3, offsetof(ModelVertex, normal) },
			{ "TEXCOORD", 0, VertexFormat::Float2, offsetof(ModelVertex, texcoord) },
			{ "COLOR", 0, VertexFormat::Float4, offsetof(ModelVertex, color) },
			{ "BLENDINDICES", 0, VertexFormat::Float4, offsetof(ModelVertex, jointIndices) },
			{ "BLENDWEIGHT", 0, VertexFormat::Float4, offsetof(ModelVertex, jointWeights) }
		};
		pipelineDesc.depthStencilFormat = DepthStencilFormat::D32Float;
		pipelineDesc.depthStencil.depthTestEnabled = true;
		pipelineDesc.depthStencil.depthWriteEnabled = true;
		pipelineDesc.depthStencil.compareOperation = CompareOperation::Less;
		result = context.resources.CreateGraphicsPipeline(
			pipeline_, std::move(vertexShader), std::move(pixelShader), pipelineDesc);
		if(!result) {
			return std::unexpected(std::move(result.error()));
		}


		// モデルの選択・CPUロード・再生設定はScene側で済ませる。ここではGPU転送だけを担当する。
		if(modelAsset_ == nullptr || modelPose_ == nullptr || modelPose_->GetAsset() != modelAsset_) {
			Shutdown();
			return std::unexpected(Error(ErrorCategory::Resource, 1, "Scene model input is not connected."));
		}
		const ModelAssetData* asset = modelAsset_;
		result = context.resources.CreateModelResource(model_, *asset);
		if(!result) {
			Shutdown();
			return std::unexpected(std::move(result.error()));
		}

		// Materialごとに独立したTexture/Samplerを生成し、SubmeshのmaterialSlotで選択する。
		constexpr std::array<uint8_t, 16> kCheckerPixels = {
			255, 255, 255, 255,  40, 110, 220, 255,
			 40, 110, 220, 255, 255, 255, 255, 255
		};
		materialResources_.reserve(asset->materials.size());
		for(const ModelMaterialAssetData& material : asset->materials) {
			MaterialGpuResource gpuMaterial;
			gpuMaterial.texture = std::make_unique<TextureResource>();
			result = material.baseColorImageData.empty()
				? resources_->CreateTexture2D(*gpuMaterial.texture,
					TextureDesc { 2, 2, TextureFormat::Rgba8UnormSrgb }, kCheckerPixels)
				: resources_->CreateTexture2DFromEncodedData(
					*gpuMaterial.texture, material.baseColorImageData);
			if(!result) {
				Shutdown();
				return std::unexpected(std::move(result.error()));
			}
			auto textureReference = resources_->CreatePersistentTextureShaderResource(*gpuMaterial.texture);
			if(!textureReference) {
				Shutdown();
				return std::unexpected(std::move(textureReference.error()));
			}
			gpuMaterial.textureRef = *textureReference;
			auto samplerReference = resources_->CreatePersistentSampler(material.sampler);
			if(!samplerReference) {
				Shutdown();
				return std::unexpected(std::move(samplerReference.error()));
			}
			gpuMaterial.samplerRef = *samplerReference;
			materialResources_.push_back(std::move(gpuMaterial));
		}

		meshDrawBuffers_.resize(model_.GetMeshCount());
		for(uint32_t meshIndex = 0; meshIndex < model_.GetMeshCount(); ++meshIndex) {
			const MeshResource* mesh = model_.GetMesh(meshIndex);
			if(mesh == nullptr) continue;
			auto& buffers = meshDrawBuffers_[meshIndex];
			buffers.reserve(mesh->GetSubmeshes().size());
			for(std::size_t submeshIndex = 0; submeshIndex < mesh->GetSubmeshes().size(); ++submeshIndex) {
				auto drawBuffer = std::make_unique<ConstantBuffer>();
				result = resources_->CreateConstantBuffer(*drawBuffer, sizeof(PrimitiveDrawData));
				if(!result) {
					Shutdown();
					return std::unexpected(std::move(result.error()));
				}
				buffers.push_back(std::move(drawBuffer));
			}
		}
		return {};
	}

	void PrimitiveRenderer::Shutdown() noexcept {
		// GraphicsSystemは通常Shutdown前にGPU idleを保証する。参照はそれでも共通retire経路へ渡し、
		// 実行中のRenderer差し替えへ拡張した場合にも即時slot再利用を起こさない。
		if(resources_ != nullptr) {
			try {
				auto retire = [&](ShaderResourceRef& reference) {
					if(!reference.IsValid()) return;
					auto result = resources_->RetirePersistentShaderResource(reference);
					if(!result) NEXUS_LOG_ERROR("Graphics", result.error().GetMessageText());
					reference = {};
				};
				for(MaterialGpuResource& material : materialResources_) {
					retire(material.textureRef);
					retire(material.samplerRef);
				}
			} catch(...) {
				NEXUS_LOG_ERROR("Graphics", "Unexpected exception while retiring PrimitiveRenderer descriptors.");
			}
		}
		meshDrawBuffers_.clear();
		extraDrawBuffers_.clear();
		materialResources_.clear();
		// PipelineとBufferはいずれもDevice依存なので、GraphicsSystemがDeviceより先に呼び出す。

		model_.Shutdown();
		pipeline_.Shutdown();
		resources_ = nullptr;
	}

	void PrimitiveRenderer::Render(GraphicsContext& context) {
		const uint32_t frameIndex = context.GetFrameIndex();
		if(meshDrawBuffers_.empty() || meshDrawBuffers_.front().empty() ||
		   frameIndex >= meshDrawBuffers_.front().front()->GetFrameCount()) {
			return;
		}
		if(context.GetRenderWidth() == 0 || context.GetRenderHeight() == 0) return;


		// Sceneがない場合は描画しない。Renderer自身がゲーム空間やCameraを補完しない。
		if(renderScene_ == nullptr || !renderScene_->camera.has_value() || modelPose_ == nullptr) return;
		const size_t objectCount = renderScene_->primitives.size();
		const Matrix4x4 projection = renderScene_->camera->viewProjectionMatrix;
		// 各Drawは独立したframe bufferを必要とする。同じCBを書き換えて複数Drawすると、
		// GPUが全Drawで最後の値を読むため、objectごとに転送領域を確保し高水位まで再利用する。
		while(extraDrawBuffers_.size() + 1 < objectCount) {
			std::vector<std::vector<std::unique_ptr<ConstantBuffer>>> buffers(model_.GetMeshCount());
			for(size_t meshIndex = 0; meshIndex < meshDrawBuffers_.size(); ++meshIndex) {
				for(size_t submesh = 0; submesh < meshDrawBuffers_[meshIndex].size(); ++submesh) {
					auto buffer = std::make_unique<ConstantBuffer>();
					auto result = resources_->CreateConstantBuffer(*buffer, sizeof(PrimitiveDrawData));
					if(!result) { NEXUS_LOG_ERROR("Graphics", result.error().GetMessageText()); return; }
					buffers[meshIndex].push_back(std::move(buffer));
				}
			}
			extraDrawBuffers_.push_back(std::move(buffers));
		}
		// Rendererは描画意図だけを記述し、Native Command List操作はGraphicsContextへ委譲する。
		context.SetGraphicsPipeline(pipeline_);
		context.SetPrimitiveTopology(PrimitiveTopology::TriangleList);
		const auto& nodeTransforms = modelPose_->GetNodeWorldTransforms();
		for(size_t objectIndex = 0; objectIndex < objectCount; ++objectIndex) {
			const Matrix4x4 instanceWorld = renderScene_->primitives[objectIndex].worldMatrix;
			auto& drawBuffers = objectIndex == 0 ? meshDrawBuffers_ : extraDrawBuffers_[objectIndex - 1];
			for(std::size_t nodeIndex = 0; nodeIndex < model_.GetNodes().size(); ++nodeIndex) {
				const ModelNode& node = model_.GetNodes()[nodeIndex];
				if(node.meshIndices.empty()) continue;
				const uint32_t skinIndex = model_.GetMeshSkinIndex(node.meshIndices.front());
				const bool hasSkin = skinIndex != MeshAssetData::kNoSkin;
				// Skin PaletteはModel空間まで変換済み。Skinned meshへnode行列を重ねて二重変換しない。
				const Matrix4x4 world = hasSkin
					? instanceWorld
					: Multiply(nodeTransforms[nodeIndex], instanceWorld);
				// Sceneから抽出されたCameraのViewProjectionを使用し、RendererにCamera設定を持たせない。
				drawData_.worldViewProjection = Multiply(world, projection);
				const auto inverseWorld = TryInverse(world);
				if(!inverseWorld) continue;
				drawData_.worldInverseTranspose = Transpose(*inverseWorld);
				drawData_.skinningEnabled = hasSkin ? 1U : 0U;
				drawData_.jointPalette.fill(Matrix4x4::Identity());
				if(hasSkin) {
					const auto* palette = modelPose_->GetSkinPalette(skinIndex);
					if(palette == nullptr || palette->size() > drawData_.jointPalette.size()) continue;
					std::copy(palette->begin(), palette->end(), drawData_.jointPalette.begin());
				}

				for(const uint32_t meshIndex : node.meshIndices) {
					const MeshResource* mesh = model_.GetMesh(meshIndex);
					if(mesh == nullptr) continue;
					context.SetMesh(*mesh);
					for(std::size_t submeshIndex = 0; submeshIndex < mesh->GetSubmeshes().size(); ++submeshIndex) {
						const SubmeshRange& submesh = mesh->GetSubmeshes()[submeshIndex];
						if(submesh.materialSlot >= materialResources_.size() ||
						   submesh.materialSlot >= model_.GetMaterials().size()) continue;
						const MaterialGpuResource& gpuMaterial = materialResources_[submesh.materialSlot];
						const ModelMaterial& material = model_.GetMaterials()[submesh.materialSlot];
						drawData_.textureIndex = gpuMaterial.textureRef.index;
						drawData_.samplerIndex = gpuMaterial.samplerRef.index;
						drawData_.tint = material.baseColorFactor;
						if(renderScene_ != nullptr) {
							for(size_t channel = 0; channel < 4; ++channel) drawData_.tint[channel] *= renderScene_->primitives[objectIndex].tint[channel];
						}

						const auto drawDataBytes = std::as_bytes(std::span(&drawData_, 1));
						ConstantBuffer& drawBuffer = *drawBuffers[meshIndex][submeshIndex];
						auto writeResult = drawBuffer.Write(frameIndex,
							{ reinterpret_cast<const uint8_t*>(drawDataBytes.data()), drawDataBytes.size() });
						if(!writeResult) {
							NEXUS_LOG_ERROR("Graphics", writeResult.error().GetMessageText());
							continue;
						}
						auto descriptorResult = context.SetGraphicsConstantBufferTable(pipeline_, drawBuffer);
						if(!descriptorResult) {
							NEXUS_LOG_ERROR("Graphics", descriptorResult.error().GetMessageText());
							continue;
						}
						context.DrawIndexed(
							submesh.indexCount, 1, submesh.firstIndex, submesh.vertexOffset, 0);
					}
				}
			}
		}
	}

} // namespace NexusEngine
