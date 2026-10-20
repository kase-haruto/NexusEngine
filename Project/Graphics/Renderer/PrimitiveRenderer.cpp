#include "PrimitiveRenderer.h"
#include "Graphics/Color/ColorSpace.h"
#include "Graphics/Meshlet/MeshletBuilder.h"
#include "Graphics/Lighting/LightmapGeometry.h"
#include "Graphics/Lighting/LightBaker.h"

// c++
#include <array>
#include <cstddef>
#include <span>
#include <utility>

// engine
#include "Graphics/Model/ModelAssetData.h"
#include "Graphics/Resource/SamplerDesc.h"
#include "GraphicsContext.h"
#include "GraphicsResourceFactory.h"

#include "Foundation/Logging/Logger.h"

namespace NexusEngine {
	Result<void> PrimitiveRenderer::BakeLighting(const BakedLightingSettings& settings) {
		if(!modelAsset_ || !modelPose_ || !renderScene_) return std::unexpected(Error(ErrorCategory::Resource,91,"Bake requires model and render scene."));
		auto baked=LightBaker{}.Bake(*modelAsset_,*modelPose_,*renderScene_,settings);
		if(!baked) return std::unexpected(std::move(baked.error()));
		return SaveBakedLighting(*baked,settings.cachePath);
	}
	Result<void> PrimitiveRenderer::SetOpticalMaterial(uint32_t slot, const OpticalMaterial& optics) {
		if(slot >= opticalMaterials_.size() || !optics.IsValid())
			return std::unexpected(Error(ErrorCategory::Graphics, 70, "Invalid optical material slot or coefficients."));
		opticalMaterials_[slot].optics = optics;
		return {};
	}
	/////////////////////////////////////////////////////////////////////////////////////////
	// Primitive描画に必要なShader、Pipeline、Vertex/Index Bufferを初期化する
	/////////////////////////////////////////////////////////////////////////////////////////
	Result<void> PrimitiveRenderer::Initialize(
		const GraphicsRendererInitializationContext& context) {
		resources_ = &context.resources;
		// Shaderはこの初期化時にCompileとReflectionを一度だけ行い、Draw中には解析しない。
		Shader vertexShader;
		auto   result = vertexShader.Initialize(
			{context.shaderDirectory / L"Primitive/Primitive.VS.hlsl", L"main", L"vs_6_6"},
			ShaderStage::Vertex);
		if(!result) {
			return std::unexpected(std::move(result.error()));
		}

		Shader pixelShader;
		result = pixelShader.Initialize(
			{context.shaderDirectory / L"Primitive/Primitive.PS.hlsl", L"main", L"ps_6_6"},
			ShaderStage::Pixel);
		if(!result) {
			return std::unexpected(std::move(result.error()));
		}

		// Reflectionでsemanticを検証しつつ、CPU構造からしか安全に決められないoffsetを明示する。
		GraphicsPipelineDesc pipelineDesc;
		pipelineDesc.hdrRenderTarget = true;
		pipelineDesc.vertexLayout = {
			{"POSITION", 0, VertexFormat::Float3, offsetof(ModelVertex, position)},
			{"NORMAL", 0, VertexFormat::Float3, offsetof(ModelVertex, normal)},
			{"TEXCOORD", 0, VertexFormat::Float2, offsetof(ModelVertex, texcoord)},
			{"COLOR", 0, VertexFormat::Float4, offsetof(ModelVertex, color)},
			{"BLENDINDICES", 0, VertexFormat::Float4, offsetof(ModelVertex, jointIndices)},
			{"BLENDWEIGHT", 0, VertexFormat::Float4, offsetof(ModelVertex, jointWeights)},
			{"TANGENT", 0, VertexFormat::Float4, offsetof(ModelVertex, tangent)},
			{"TEXCOORD", 1, VertexFormat::Float2, offsetof(ModelVertex, lightmapUV)}};
		pipelineDesc.depthStencilFormat				= DepthStencilFormat::D32Float;
		pipelineDesc.depthStencil.depthTestEnabled	= true;
		pipelineDesc.depthStencil.depthWriteEnabled = true;
		pipelineDesc.depthStencil.compareOperation	= CompareOperation::Less;
		result										= context.resources.CreateGraphicsPipeline(
			pipeline_, std::move(vertexShader), std::move(pixelShader), pipelineDesc);
		if(!result) {
			return std::unexpected(std::move(result.error()));
		}

		// モデルの選択・CPUロード・再生設定はScene側で済ませる。ここではGPU転送だけを担当する。
		if(modelAsset_ == nullptr || modelPose_ == nullptr || modelPose_->GetAsset() != modelAsset_) {
			Shutdown();
			return std::unexpected(Error(ErrorCategory::Resource, 1, "Scene model input is not connected."));
		}
		// 共有CPUモデルを変更せず、独立UV付きGPU形状を生成する。材質UVとSkinningは維持する。
		ModelAssetData prepared = *modelAsset_;
		result = GenerateLightmapGeometry(prepared);
		if(!result) { Shutdown(); return result; }
		const ModelAssetData* asset = &prepared;
		result						= context.resources.CreateModelResource(model_, *asset);
		if(!result) {
			Shutdown();
			return std::unexpected(std::move(result.error()));
		}

		// Materialごとに独立したTexture/Samplerを生成し、SubmeshのmaterialSlotで選択する。
		// Missing albedo maps multiply by white; a checker fallback would tint mirrors.
		constexpr std::array<uint8_t, 16> kWhitePixels = {
			255, 255, 255, 255, 255, 255, 255, 255,
			255, 255, 255, 255, 255, 255, 255, 255};
		materialResources_.reserve(asset->materials.size());
		for(const ModelMaterialAssetData& material : asset->materials) {
			// Retain ownership before allocation so every failure retires its descriptors.
			materialResources_.emplace_back();
			auto& gpuMaterial = materialResources_.back();
			gpuMaterial.texture = std::make_unique<TextureResource>();
			result				= material.baseColorImageData.empty()
									  ? resources_->CreateTexture2D(*gpuMaterial.texture,
																	TextureDesc{2, 2, TextureFormat::Rgba8UnormSrgb}, kWhitePixels)
									  : resources_->CreateTexture2DFromEncodedData(
											*gpuMaterial.texture, material.baseColorImageData, TextureColorSpace::Srgb);
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
			auto samplerReference  = resources_->CreatePersistentSampler(material.sampler);
			if(!samplerReference) {
				Shutdown();
				return std::unexpected(std::move(samplerReference.error()));
			}
			gpuMaterial.samplerRef = *samplerReference;
			// Retain ownership before upload so initialization failures retire every descriptor.
			auto uploadMap = [&](const std::vector<uint8_t>& bytes, const SamplerDesc& sampler,
				std::unique_ptr<TextureResource>& texture, ShaderResourceRef& ref, ShaderResourceRef& samplerRef, TextureColorSpace space = TextureColorSpace::Linear) -> Result<void> {
				if(bytes.empty()) return {};
				texture = std::make_unique<TextureResource>();
				auto status = resources_->CreateTexture2DFromEncodedData(*texture, bytes, space);
				if(!status) return status;
				auto srv = resources_->CreatePersistentTextureShaderResource(*texture);
				if(!srv) return std::unexpected(std::move(srv.error()));
				ref = *srv;
				auto sample = resources_->CreatePersistentSampler(sampler);
				if(!sample) return std::unexpected(std::move(sample.error()));
				samplerRef = *sample;
				return {};
			};
			auto& gpu = materialResources_.back();
			result = uploadMap(material.normalImageData, material.normalSampler, gpu.normalTexture, gpu.normalRef, gpu.normalSamplerRef);
			if(result) result = uploadMap(material.metallicRoughnessImageData, material.metallicRoughnessSampler,
				gpu.metallicRoughnessTexture, gpu.metallicRoughnessRef, gpu.metallicRoughnessSamplerRef);
			if(result) result = uploadMap(material.emissiveImageData, material.emissiveSampler, gpu.emissiveTexture, gpu.emissiveRef, gpu.emissiveSamplerRef, TextureColorSpace::Srgb);
			if(result) result = uploadMap(material.occlusionImageData, material.occlusionSampler, gpu.occlusionTexture, gpu.occlusionRef, gpu.occlusionSamplerRef);
			if(!result) { Shutdown(); return std::unexpected(std::move(result.error())); }
		}

		meshDrawBuffers_.resize(model_.GetMeshCount());
		for(uint32_t meshIndex = 0; meshIndex < model_.GetMeshCount(); ++meshIndex) {
			const MeshResource* mesh = model_.GetMesh(meshIndex);
			if(mesh == nullptr) continue;
			auto& buffers = meshDrawBuffers_[meshIndex];
			buffers.reserve(mesh->GetSubmeshes().size());
			for(std::size_t submeshIndex = 0; submeshIndex < mesh->GetSubmeshes().size(); ++submeshIndex) {
				auto drawBuffer = std::make_unique<ConstantBuffer>();
				result			= resources_->CreateConstantBuffer(*drawBuffer, sizeof(PrimitiveDrawData));
				if(!result) {
					Shutdown();
					return std::unexpected(std::move(result.error()));
				}
				buffers.push_back(std::move(drawBuffer));
			}
		}
		result = resources_->CreateShadowSystem(shadows_, context.shaderDirectory);
		if(!result) {
			Shutdown();
			return result;
		}
		result = optics_.Initialize(*resources_, context.shaderDirectory);
		if(!result) {
			Shutdown();
			return result;
		}
		result = environment_.Initialize(context.resources, context.shaderDirectory);
		if(!result) { Shutdown(); return result; }
		bakedLighting_.Initialize(context.resources);
		for(size_t i = 0; i < model_.GetMaterials().size(); ++i) {
			const auto& material = model_.GetMaterials()[i];
			opticalMaterials_.push_back({material.baseColorFactor, material.optics,
				materialResources_[i].textureRef, materialResources_[i].samplerRef,
				{material.metallicFactor, material.roughnessFactor, material.normalScale, 0},
				materialResources_[i].normalRef, materialResources_[i].normalSamplerRef,
				materialResources_[i].metallicRoughnessRef, materialResources_[i].metallicRoughnessSamplerRef,
				{material.emissiveFactor[0], material.emissiveFactor[1], material.emissiveFactor[2], material.occlusionStrength},
				materialResources_[i].emissiveRef, materialResources_[i].emissiveSamplerRef, materialResources_[i].occlusionRef, materialResources_[i].occlusionSamplerRef});
		}
		////////////////////////////////////////////////////////////////////////////////
		// 静的 Meshlet は Asset 初期化時だけ生成。Skinning は従来 Pipeline を維持する。
		// Mesh Pipeline / 個別 Meshlet 初期化失敗も従来描画を継続できるようにする。
		// 他の必須初期化が成功した後に実行し、Upload 完了確認に失敗した Resource が
		// 初期化 rollback で即時破棄されず、GPU idle 後の Shutdown まで保持されるようにする。
		////////////////////////////////////////////////////////////////////////////////
		meshletResources_.resize(model_.GetMeshCount());
		if(resources_->GetCapabilities().SupportsMeshShaders()) {
			auto meshStatus = resources_->CreateMeshShaderPipeline(meshPipeline_, context.shaderDirectory);
			if(!meshStatus) NEXUS_LOG_WARNING("Meshlet", meshStatus.error().GetMessageText());
			else for(uint32_t i = 0; i < model_.GetMeshCount(); ++i) {
				if(asset->meshes[i].skinIndex != MeshAssetData::kNoSkin) continue;
				auto data = MeshletBuilder{}.Build(asset->meshes[i]);
				if(!data) { NEXUS_LOG_WARNING("Meshlet", data.error().GetMessageText()); continue; }
				auto resource = std::make_unique<MeshletResource>();
				meshStatus = resources_->CreateMeshletResource(*resource, *data, *model_.GetMesh(i));
				if(!meshStatus) {
					NEXUS_LOG_WARNING("Meshlet", meshStatus.error().GetMessageText());
					// Upload 待機失敗時にも GPU が使用する Resource を Shutdown まで保持する。
					meshletResources_[i] = std::move(resource);
					continue;
				}
				meshletResources_[i] = std::move(resource);
			}
		}
		return {};
	}

	void PrimitiveRenderer::Shutdown() noexcept {
		// GraphicsSystem の GPU idle 後に呼ばれる。元 Vertex Buffer より先に解放する。
		meshletResources_.clear();
		meshPipeline_.Shutdown();
		optics_.Shutdown();
		environment_.Shutdown();
		bakedLighting_.Shutdown();
		opticalDraws_.clear();
		opticalMaterials_.clear();
		shadows_.Shutdown();
		shadowDraws_.clear();
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
					retire(material.normalRef); retire(material.normalSamplerRef);
					retire(material.metallicRoughnessRef); retire(material.metallicRoughnessSamplerRef);
					retire(material.emissiveRef); retire(material.emissiveSamplerRef);
					retire(material.occlusionRef); retire(material.occlusionSamplerRef);
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
		optics_.ClearFrameOutputs();
		drawData_.diffuseGIIndex=0;
		drawData_.ddgi={};
		const uint32_t frameIndex = context.GetFrameIndex();
		if(meshDrawBuffers_.empty() || meshDrawBuffers_.front().empty() ||
		   frameIndex >= meshDrawBuffers_.front().front()->GetFrameCount()) {
			return;
		}
		if(context.GetRenderWidth() == 0 || context.GetRenderHeight() == 0) return;

		// Sceneがない場合は描画しない。Renderer自身がゲーム空間やCameraを補完しない。
		if(renderScene_ == nullptr || !renderScene_->camera.has_value() || modelPose_ == nullptr) return;
		const size_t	objectCount = renderScene_->primitives.size();
		const Matrix4x4 projection	= renderScene_->camera->viewProjectionMatrix;
		const auto cameraWorld = TryInverse(renderScene_->camera->viewMatrix);
		if(!cameraWorld) return;
		auto environmentStatus = environment_.Prepare(renderScene_->environment);
		if(!environmentStatus) NEXUS_LOG_ERROR("Environment", environmentStatus.error().GetMessageText());
		drawData_.environment = environment_.GetDrawData(renderScene_->environment);
		const bool dynamicGI=renderScene_->ddgi.enabled && resources_->GetCapabilities().SupportsRayTracing();
		auto bakedStatus = dynamicGI?Result<void>{}:bakedLighting_.Prepare(*modelAsset_,*modelPose_,*renderScene_);
		if(!bakedStatus) NEXUS_LOG_ERROR("BakedGI",bakedStatus.error().GetMessageText());
		auto bakedSettings=renderScene_->bakedLighting;
		if(dynamicGI) bakedSettings.enabled=false;
		const auto bakedData = bakedLighting_.GetDrawData(bakedSettings);
		auto realtimeGI = renderScene_->globalIllumination;
		// ベイク方式を選択した場合はキャッシュ未作成時もRTGIを起動しない。
		if(renderScene_->bakedLighting.enabled || renderScene_->ddgi.enabled) realtimeGI.enabled = false;
		drawData_.cameraPosition = {cameraWorld->At(3, 0), cameraWorld->At(3, 1), cameraWorld->At(3, 2), 1};
		for(size_t i = 0; i < 3; ++i) drawData_.ambientIrradiance[i] = renderScene_->ambientIrradiance[i];
		const auto& fog = renderScene_->fog;
		const bool validFog = fog.IsValid();
		drawData_.fogColorEnabled = validFog ? std::array<float, 4>{fog.color[0], fog.color[1], fog.color[2], fog.enabled ? 1.0f : 0.0f}
			: std::array<float, 4>{};
		drawData_.fogDistances = validFog ? std::array<float, 4>{fog.startDistance, fog.endDistance, 0, 0}
			: std::array<float, 4>{0, 1, 0, 0};
		// Extract Geometry from the already available draw snapshot, never walk Scene/ECS
		// again. Static meshes are shared by all Instances, while transforms remain per-instance.
		shadowDraws_.clear();
		const auto& shadowNodeTransforms = modelPose_->GetNodeWorldTransforms();
		opticalDraws_.clear();
		for(const auto& object : renderScene_->primitives) {
			for(size_t nodeIndex = 0; nodeIndex < model_.GetNodes().size(); ++nodeIndex) {
				if(object.nodeIndex != PrimitiveRenderItem::kAllNodes && object.nodeIndex != nodeIndex) continue;
				const auto& node = model_.GetNodes()[nodeIndex];
				for(const uint32_t meshIndex : node.meshIndices) {
					const auto* mesh = model_.GetMesh(meshIndex);
					if(!mesh) continue;
					const uint32_t skin	   = model_.GetMeshSkinIndex(meshIndex);
					const bool	   skinned = skin != MeshAssetData::kNoSkin;
					ShadowDrawItem draw;
					draw.mesh		= mesh;
					draw.instanceId = object.sourceObject.value;
					draw.world		= skinned ? object.worldMatrix : Multiply(shadowNodeTransforms[nodeIndex], object.worldMatrix);
					if(!TryInverse(draw.world)) continue; // Match Forward's non-invertible transform rejection.
					if(skinned) {
						const auto* palette = modelPose_->GetSkinPalette(skin);
						if(!palette || palette->size() > kMaxSkinJoints) continue;
						draw.jointPalette = *palette;
					}
					shadowDraws_.push_back(draw);
					// VSだけで変形するSkinningはASへ投入できない。DDGIの受光自体はRasterで利用する。
					if(!dynamicGI || !skinned) opticalDraws_.push_back({draw, object.tint});
				}
			}
		}
		auto radiance = (renderScene_->environment.debugMode == LightingDebugMode::Final || renderScene_->environment.debugMode == LightingDebugMode::Indirect || realtimeGI.enabled || renderScene_->ddgi.enabled)
			? optics_.Render(context, projection, renderScene_->directionalLight, opticalDraws_, opticalMaterials_, renderScene_->ambientIrradiance, drawData_.environment, realtimeGI, bakedData, renderScene_->ddgi)
			: Result<ShaderResourceRef>{ShaderResourceRef{}};
		drawData_.diffuseGIIndex = radiance && realtimeGI.enabled ? optics_.GetDiffuseGlobalIllumination().index : 0;
		drawData_.ddgi=radiance && renderScene_->ddgi.enabled?optics_.GetDDGIDrawData():DDGIDrawData{};
		if(!radiance) {
			NEXUS_LOG_ERROR("Optics", radiance.error().GetMessageText());
			drawData_.opticalRadianceIndex = 0;
		} else
			drawData_.opticalRadianceIndex = radiance->index;
		auto mask = shadows_.Render(context, projection, renderScene_->directionalLight, shadowDraws_);
		if(!mask) {
			NEXUS_LOG_ERROR("Shadow", mask.error().GetMessageText());
			drawData_.shadowMaskIndex = 0;
		} else
			drawData_.shadowMaskIndex = mask->index;
		const auto& light				  = renderScene_->directionalLight;
		drawData_.lightDirectionIntensity = {light.direction[0], light.direction[1], light.direction[2], light.intensity};
		drawData_.lightColor			  = {light.color[0], light.color[1], light.color[2], 0};
		// 各Drawは独立したframe bufferを必要とする。同じCBを書き換えて複数Drawすると、
		// GPUが全Drawで最後の値を読むため、objectごとに転送領域を確保し高水位まで再利用する。
		while(extraDrawBuffers_.size() + 1 < objectCount) {
			std::vector<std::vector<std::unique_ptr<ConstantBuffer>>> buffers(model_.GetMeshCount());
			extraDrawBuffers_.push_back(std::move(buffers));
		}
		// NodeごとのEntityを増やしても全Mesh分のCBをEntityごとに確保しない。
		for(size_t objectIndex = 1; objectIndex < objectCount; ++objectIndex) {
			auto&	   buffers		= extraDrawBuffers_[objectIndex - 1];
			const auto selectedNode = renderScene_->primitives[objectIndex].nodeIndex;
			for(size_t meshIndex = 0; meshIndex < meshDrawBuffers_.size(); ++meshIndex) {
				if(selectedNode != PrimitiveRenderItem::kAllNodes) {
					if(selectedNode >= model_.GetNodes().size()) continue;
					const auto& meshes = model_.GetNodes()[selectedNode].meshIndices;
					if(std::find(meshes.begin(), meshes.end(), static_cast<uint32_t>(meshIndex)) == meshes.end()) continue;
				}
				while(buffers[meshIndex].size() < meshDrawBuffers_[meshIndex].size()) {
					auto buffer = std::make_unique<ConstantBuffer>();
					auto result = resources_->CreateConstantBuffer(*buffer, sizeof(PrimitiveDrawData));
					if(!result) {
						NEXUS_LOG_ERROR("Graphics", result.error().GetMessageText());
						return;
					}
					buffers[meshIndex].push_back(std::move(buffer));
				}
			}
		}
		// Rendererは描画意図だけを記述し、Native Command List操作はGraphicsContextへ委譲する。
		context.SetGraphicsPipeline(pipeline_);
		bool meshPipelineBound = false; //< Mesh -> Traditional の切替時だけ PSO を復元する
		context.SetPrimitiveTopology(PrimitiveTopology::TriangleList);
		const auto& nodeTransforms = modelPose_->GetNodeWorldTransforms();
		for(size_t objectIndex = 0; objectIndex < objectCount; ++objectIndex) {
			const Matrix4x4 instanceWorld = renderScene_->primitives[objectIndex].worldMatrix;
			auto&			drawBuffers	  = objectIndex == 0 ? meshDrawBuffers_ : extraDrawBuffers_[objectIndex - 1];
			for(std::size_t nodeIndex = 0; nodeIndex < model_.GetNodes().size(); ++nodeIndex) {
				const auto selectedNode = renderScene_->primitives[objectIndex].nodeIndex;
				if(selectedNode != PrimitiveRenderItem::kAllNodes && selectedNode != nodeIndex) continue;
				const ModelNode& node = model_.GetNodes()[nodeIndex];
				if(node.meshIndices.empty()) continue;
				const uint32_t skinIndex = model_.GetMeshSkinIndex(node.meshIndices.front());
				const bool	   hasSkin	 = skinIndex != MeshAssetData::kNoSkin;
				// Skin PaletteはModel空間まで変換済み。Skinned meshへnode行列を重ねて二重変換しない。
				const Matrix4x4 world = hasSkin
											? instanceWorld
											: Multiply(nodeTransforms[nodeIndex], instanceWorld);
				// Sceneから抽出されたCameraのViewProjectionを使用し、RendererにCamera設定を持たせない。
				drawData_.worldViewProjection = Multiply(world, projection);
				drawData_.world = world;
				const auto inverseWorld		  = TryInverse(world);
				if(!inverseWorld) continue;
				drawData_.worldInverseTranspose = Transpose(*inverseWorld);
				drawData_.skinningEnabled		= hasSkin ? 1U : 0U;
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
					const auto& object = renderScene_->primitives[objectIndex];
					drawData_.bakedLighting = bakedLighting_.GetDrawData(bakedSettings,
						object.lightingId ? object.lightingId : object.sourceObject.value,static_cast<uint32_t>(nodeIndex),meshIndex,world,object.staticLighting && !hasSkin);
					for(std::size_t submeshIndex = 0; submeshIndex < mesh->GetSubmeshes().size(); ++submeshIndex) {
						const SubmeshRange& submesh = mesh->GetSubmeshes()[submeshIndex];
						if(submesh.materialSlot >= materialResources_.size() ||
						   submesh.materialSlot >= model_.GetMaterials().size()) continue;
						const MaterialGpuResource& gpuMaterial = materialResources_[submesh.materialSlot];
						const ModelMaterial&	   material	   = model_.GetMaterials()[submesh.materialSlot];
						drawData_.textureIndex				   = gpuMaterial.textureRef.index;
						drawData_.samplerIndex				   = gpuMaterial.samplerRef.index;
						drawData_.normalTextureIndex = gpuMaterial.normalRef.index;
						drawData_.normalSamplerIndex = gpuMaterial.normalSamplerRef.index;
						drawData_.metallicRoughnessTextureIndex = gpuMaterial.metallicRoughnessRef.index;
						drawData_.metallicRoughnessSamplerIndex = gpuMaterial.metallicRoughnessSamplerRef.index;
						drawData_.pbrParameters = {material.metallicFactor, material.roughnessFactor, material.normalScale, 0};
						drawData_.emissiveOcclusion = {material.emissiveFactor[0], material.emissiveFactor[1], material.emissiveFactor[2], material.occlusionStrength};
						drawData_.extraMaps = {gpuMaterial.emissiveRef.index, gpuMaterial.emissiveSamplerRef.index, gpuMaterial.occlusionRef.index, gpuMaterial.occlusionSamplerRef.index};
						drawData_.tint						   = material.baseColorFactor;
						if(renderScene_ != nullptr) {
							for(size_t channel = 0; channel < 4; ++channel) drawData_.tint[channel] *= renderScene_->primitives[objectIndex].tint[channel];
						}

						const auto		drawDataBytes = std::as_bytes(std::span(&drawData_, 1));
						ConstantBuffer& drawBuffer	  = *drawBuffers[meshIndex][submeshIndex];
						auto			writeResult	  = drawBuffer.Write(frameIndex,
																		 {reinterpret_cast<const uint8_t*>(drawDataBytes.data()), drawDataBytes.size()});
						if(!writeResult) {
							NEXUS_LOG_ERROR("Graphics", writeResult.error().GetMessageText());
							continue;
						}
						////////////////////////////////////////////////////////////////////////////////
						// 上位 Renderer は DispatchMesh API を知らず、描画意図だけを Context へ渡す。
						// 同じ独立 Frame CB を Material/Camera 入力に使用し、追加の毎Frame Resource を作らない。
						////////////////////////////////////////////////////////////////////////////////
						if(!hasSkin && meshIndex < meshletResources_.size() && meshletResources_[meshIndex] &&
						   context.DispatchMeshlets(meshPipeline_, *meshletResources_[meshIndex], *mesh, drawBuffer,
						       MeshCullingView{drawData_.worldViewProjection}, static_cast<uint32_t>(submeshIndex), meshletSettings_)) {
							meshPipelineBound = true;
							continue;
						}
						// Mesh draw の後は従来 Root Signature / PSO を復元してから Table を設定する。
						if(meshPipelineBound) { context.SetGraphicsPipeline(pipeline_); meshPipelineBound = false; }
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
