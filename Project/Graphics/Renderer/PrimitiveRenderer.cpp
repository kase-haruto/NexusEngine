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
#include "Graphics/Model/ModelLoader.h"
#include "Foundation/Logging/Logger.h"

namespace NexusEngine {
	namespace {
		constexpr float kPi = 3.14159265358979323846f;
		constexpr float kRotationSpeedX = 0.45f;
		constexpr float kRotationSpeedY = 0.75f;

		/*-----------------------------------------------------------------------------------------
		 * PrimitiveVertex
		 * - Primitive Shaderへ入力する位置と頂点色のCPU Layout
		 *---------------------------------------------------------------------------------------*/
	} // namespace

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

		// Khronos公式SimpleSkinを使い、外部Bufferを含むglTFのMesh/Skin/Animation経路を検証する。
		const ModelLoader modelLoader;
		auto cubeAsset = modelLoader.LoadGltf(
			context.assetDirectory / L"Models/SimpleSkin/SimpleSkin.gltf");
		if(!cubeAsset) {
			pipeline_.Shutdown();
			return std::unexpected(std::move(cubeAsset.error()));
		}
		result = context.resources.CreateModelResource(cubeModel_, *cubeAsset);
		if(!result) {
			pipeline_.Shutdown();
			return std::unexpected(std::move(result.error()));
		}
		result = cubeInstance_.Initialize(cubeModel_);
		if(!result) {
			Shutdown();
			return std::unexpected(std::move(result.error()));
		}
		if(!cubeModel_.GetAnimations().empty()) {
			result = cubeInstance_.Play(0, true);
			if(!result) {
				Shutdown();
				return std::unexpected(std::move(result.error()));
			}
		}

		// Materialごとに独立したTexture/Samplerを生成し、SubmeshのmaterialSlotで選択する。
		constexpr std::array<uint8_t, 16> kCheckerPixels = {
			255, 255, 255, 255,  40, 110, 220, 255,
			 40, 110, 220, 255, 255, 255, 255, 255
		};
		materialResources_.reserve(cubeAsset->materials.size());
		for(const ModelMaterialAssetData& material : cubeAsset->materials) {
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

		meshDrawBuffers_.resize(cubeModel_.GetMeshCount());
		for(uint32_t meshIndex = 0; meshIndex < cubeModel_.GetMeshCount(); ++meshIndex) {
			const MeshResource* mesh = cubeModel_.GetMesh(meshIndex);
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
		animationStartTime_ = std::chrono::steady_clock::now();
		lastAnimationUpdateTime_ = animationStartTime_;
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
		materialResources_.clear();
		// PipelineとBufferはいずれもDevice依存なので、GraphicsSystemがDeviceより先に呼び出す。
		cubeInstance_.Shutdown();
		cubeModel_.Shutdown();
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

		// 絶対的な経過時間から回転を再構築し、フレームレートの揺らぎで回転速度が変わらないようにする。
		const auto currentTime = std::chrono::steady_clock::now();
		const float elapsedSeconds = std::chrono::duration<float>(currentTime - animationStartTime_).count();
		const float animationDelta = std::chrono::duration<float>(currentTime - lastAnimationUpdateTime_).count();
		lastAnimationUpdateTime_ = currentTime;
		cubeInstance_.Update(animationDelta);
		const Quaternion rotation = Quaternion::FromEulerRadians({
			elapsedSeconds * kRotationSpeedX,
			elapsedSeconds * kRotationSpeedY,
			0.0f
		});
		const Matrix4x4 instanceWorld = MakeAffineMatrix(
			{ 0.75f, 0.75f, 0.75f }, rotation, { 0.0f, -0.75f, 3.0f });
		const float aspectRatio = static_cast<float>(context.GetRenderWidth()) /
			static_cast<float>(context.GetRenderHeight());
		const Matrix4x4 projection = MakePerspectiveFovMatrix(
			kPi / 3.0f, aspectRatio, 0.1f, 100.0f);
		// Rendererは描画意図だけを記述し、Native Command List操作はGraphicsContextへ委譲する。
		context.SetGraphicsPipeline(pipeline_);
		context.SetPrimitiveTopology(PrimitiveTopology::TriangleList);
		const auto& nodeTransforms = cubeInstance_.GetNodeWorldTransforms();
		for(std::size_t nodeIndex = 0; nodeIndex < cubeModel_.GetNodes().size(); ++nodeIndex) {
			const ModelNode& node = cubeModel_.GetNodes()[nodeIndex];
			if(node.meshIndices.empty()) continue;
			const uint32_t skinIndex = cubeModel_.GetMeshSkinIndex(node.meshIndices.front());
			const bool hasSkin = skinIndex != MeshAssetData::kNoSkin;
			// Skin PaletteはModel空間まで変換済み。Skinned meshへnode行列を重ねて二重変換しない。
			const Matrix4x4 world = hasSkin
				? instanceWorld
				: Multiply(nodeTransforms[nodeIndex], instanceWorld);
			// Cameraはoriginから+Zを向くためViewはIdentity。node/model/world/projectionの順に合成する。
			drawData_.worldViewProjection = Multiply(world, projection);
			const auto inverseWorld = TryInverse(world);
			if(!inverseWorld) continue;
			drawData_.worldInverseTranspose = Transpose(*inverseWorld);
			drawData_.skinningEnabled = hasSkin ? 1U : 0U;
			drawData_.jointPalette.fill(Matrix4x4::Identity());
			if(hasSkin) {
				const auto* palette = cubeInstance_.GetSkinPalette(skinIndex);
				if(palette == nullptr || palette->size() > drawData_.jointPalette.size()) continue;
				std::copy(palette->begin(), palette->end(), drawData_.jointPalette.begin());
			}

			for(const uint32_t meshIndex : node.meshIndices) {
				const MeshResource* mesh = cubeModel_.GetMesh(meshIndex);
				if(mesh == nullptr) continue;
				context.SetMesh(*mesh);
				for(std::size_t submeshIndex = 0; submeshIndex < mesh->GetSubmeshes().size(); ++submeshIndex) {
					const SubmeshRange& submesh = mesh->GetSubmeshes()[submeshIndex];
					if(submesh.materialSlot >= materialResources_.size() ||
					   submesh.materialSlot >= cubeModel_.GetMaterials().size()) continue;
					const MaterialGpuResource& gpuMaterial = materialResources_[submesh.materialSlot];
					const ModelMaterial& material = cubeModel_.GetMaterials()[submesh.materialSlot];
					drawData_.textureIndex = gpuMaterial.textureRef.index;
					drawData_.samplerIndex = gpuMaterial.samplerRef.index;
					drawData_.tint = material.baseColorFactor;

					const auto drawDataBytes = std::as_bytes(std::span(&drawData_, 1));
					ConstantBuffer& drawBuffer = *meshDrawBuffers_[meshIndex][submeshIndex];
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

} // namespace NexusEngine
