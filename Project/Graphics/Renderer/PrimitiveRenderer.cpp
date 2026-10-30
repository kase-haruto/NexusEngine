#include "PrimitiveRenderer.h"
#include "Graphics/Color/ColorSpace.h"
#include "Graphics/Meshlet/MeshletBuilder.h"
#include "Graphics/Lighting/LightmapGeometry.h"
#include "Graphics/Lighting/LightBaker.h"

// c++
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <utility>
#include <chrono>

// engine
#include "Graphics/Model/ModelAssetData.h"
#include "Graphics/Resource/SamplerDesc.h"
#include "GraphicsContext.h"
#include "GraphicsResourceFactory.h"

#include "Foundation/Logging/Logger.h"

namespace NexusEngine {
	struct RendererModelResources {
		std::unordered_map<uint64_t, std::shared_ptr<const MeshResource>> meshes;
		std::unordered_map<uint64_t, std::shared_ptr<PrimitiveRenderer::SharedTexture>> textures;
		std::unordered_map<uint64_t, std::shared_ptr<MeshletResource>> meshlets;
		std::vector<std::pair<SamplerDesc, std::shared_ptr<PrimitiveRenderer::SharedSampler>>> samplers;
		bool meshletsEnabled = false;
	};
	PrimitiveRenderer::SharedTexture::~SharedTexture() {
		if(resources && reference.IsValid()) {
			auto retired = resources->RetirePersistentShaderResource(reference);
			if(!retired) NEXUS_LOG_ERROR("Graphics", retired.error().GetMessageText());
		}
	}
	PrimitiveRenderer::SharedSampler::~SharedSampler() {
		if(resources && reference.IsValid()) {
			auto retired = resources->RetirePersistentShaderResource(reference);
			if(!retired) NEXUS_LOG_ERROR("Graphics", retired.error().GetMessageText());
		}
	}
	std::shared_ptr<const RendererModelResources> PrimitiveRenderer::CaptureModelResources() const {
		auto reuse = std::make_shared<RendererModelResources>();
		for(size_t i = 0; i < model_.meshes_.size(); ++i) {
			const auto id = model_.sourceGeometryIds_[i];
			reuse->meshes.emplace(id, model_.meshes_[i]);
			if(i < meshletResources_.size() && meshletResources_[i]) reuse->meshlets.emplace(id, meshletResources_[i]);
		}
		reuse->textures = modelTextures_;
		reuse->samplers = materialSamplers_;
		reuse->meshletsEnabled = meshletSettings_.enabled && meshPipeline_.IsInitialized();
		return reuse;
	}
	Result<ShaderResourceRef> PrimitiveRenderer::GetMaterialSampler(const SamplerDesc& description, const RendererModelResources* reuse) {
		for(const auto& [existing, sampler] : materialSamplers_) if(existing == description) return sampler->reference;
		if(reuse) for(const auto& [existing, sampler] : reuse->samplers) if(existing == description) {
			materialSamplers_.emplace_back(description, sampler); return sampler->reference;
		}
		auto sampler = std::make_shared<SharedSampler>(); sampler->resources = resources_;
		auto created = resources_->CreatePersistentSampler(description);
		if(!created) return std::unexpected(std::move(created.error()));
		sampler->reference = *created;
		materialSamplers_.emplace_back(description, sampler);
		return sampler->reference;
	}
	Result<void> PrimitiveRenderer::InitializeShared(const GraphicsRendererInitializationContext& context,
		std::shared_ptr<const RendererModelResources> reuse) {
		initializationReuse_ = std::move(reuse);
		auto initialized = Initialize(context);
		initializationReuse_.reset();
		return initialized;
	}
	Result<std::shared_ptr<PrimitiveRenderer::SharedTexture>> PrimitiveRenderer::GetMaterialTexture(
		uint64_t id, const std::vector<uint8_t>& bytes, TextureColorSpace space, const RendererModelResources* reuse) {
		if(auto found = modelTextures_.find(id); found != modelTextures_.end()) return found->second;
		if(reuse) if(auto found = reuse->textures.find(id); found != reuse->textures.end()) {
			modelTextures_.emplace(id, found->second); ++initializationProfile_.texturesReused; return found->second;
		}
		auto texture = std::make_shared<SharedTexture>(); texture->resources = resources_;
		constexpr std::array<uint8_t, 4> white{255,255,255,255};
		auto created = bytes.empty()
			? resources_->CreateTexture2D(texture->texture, TextureDesc{1,1,TextureFormat::Rgba8UnormSrgb}, white)
			: resources_->CreateTexture2DFromEncodedData(texture->texture, bytes, space);
		if(!created) return std::unexpected(std::move(created.error()));
		auto reference = resources_->CreatePersistentTextureShaderResource(texture->texture);
		if(!reference) return std::unexpected(std::move(reference.error()));
		texture->reference = *reference;
		modelTextures_.emplace(id, texture); ++initializationProfile_.texturesCreated;
		return texture;
	}
	Result<void> PrimitiveRenderer::BuildModelResources(const GraphicsRendererInitializationContext& context,
		const RendererModelResources* reuse, std::stop_token cancellation) {
		if(!modelAsset_ || !modelPose_ || modelPose_->GetAsset() != modelAsset_)
			return std::unexpected(Error(ErrorCategory::Resource, 1, "Scene model input is not connected."));
		const auto start = std::chrono::steady_clock::now();
		const auto cancelled = [&] { return cancellation.stop_requested(); };
		const auto stopped = [] { return std::unexpected(Error(ErrorCategory::Resource, 132, "Asset preparation cancelled.")); };
		if(cancelled()) return stopped();
		const auto& asset = *modelAsset_;
		initializationProfile_.meshes = asset.meshes.size(); initializationProfile_.materials = asset.materials.size();
		for(const auto& mesh : asset.meshes) {
			if(mesh.vertexStride) initializationProfile_.vertices += mesh.vertexData.size()/mesh.vertexStride;
		}
		for(const auto& material : asset.materials)
			initializationProfile_.encodedTextureBytes += material.baseColorImageData.size() + material.normalImageData.size() +
				material.metallicRoughnessImageData.size() + material.emissiveImageData.size() + material.occlusionImageData.size();
		auto result = asset.meshes.empty() ? Result<void>{} : context.resources.CreateModelResource(model_, asset, true,
			reuse ? &reuse->meshes : nullptr, cancellation);
		if(!result) return result;
		for(uint32_t i = 0; i < model_.GetMeshCount(); ++i) {
			const auto found = reuse ? reuse->meshes.find(asset.meshes[i].resourceId) : std::unordered_map<uint64_t, std::shared_ptr<const MeshResource>>::const_iterator{};
			if(reuse && found != reuse->meshes.end() && found->second->GetGeometryGeneration() == model_.GetMesh(i)->GetGeometryGeneration())
				++initializationProfile_.meshesReused;
			else ++initializationProfile_.meshesUploaded;
		}
		const auto geometryDone = std::chrono::steady_clock::now();
		initializationProfile_.geometryMs = std::chrono::duration<double, std::milli>(geometryDone - start).count();
		materialResources_.reserve(asset.materials.size());
		for(const auto& material : asset.materials) {
			if(cancelled()) return stopped();
			materialResources_.emplace_back(); auto& gpu = materialResources_.back();
			const auto upload = [&](uint64_t id, const std::vector<uint8_t>& bytes, const SamplerDesc& sampler,
				std::shared_ptr<SharedTexture>& texture, ShaderResourceRef& reference, ShaderResourceRef& sample,
				TextureColorSpace space, bool required = false) -> Result<void> {
				if(cancelled()) return stopped();
				if(bytes.empty() && !required) return {};
				auto image = GetMaterialTexture(bytes.empty() ? 0 : id, bytes, space, reuse);
				if(!image) return std::unexpected(std::move(image.error()));
				texture = *image; reference = texture->reference;
				auto sampling = GetMaterialSampler(sampler, reuse);
				if(!sampling) return std::unexpected(std::move(sampling.error()));
				sample = *sampling; return {};
			};
			result = upload(material.baseColorImageId, material.baseColorImageData, material.sampler,
				gpu.texture, gpu.textureRef, gpu.samplerRef, TextureColorSpace::Srgb, true);
			if(result) result = upload(material.normalImageId, material.normalImageData, material.normalSampler,
				gpu.normalTexture, gpu.normalRef, gpu.normalSamplerRef, TextureColorSpace::Linear);
			if(result) result = upload(material.metallicRoughnessImageId, material.metallicRoughnessImageData, material.metallicRoughnessSampler,
				gpu.metallicRoughnessTexture, gpu.metallicRoughnessRef, gpu.metallicRoughnessSamplerRef, TextureColorSpace::Linear);
			if(result) result = upload(material.emissiveImageId, material.emissiveImageData, material.emissiveSampler,
				gpu.emissiveTexture, gpu.emissiveRef, gpu.emissiveSamplerRef, TextureColorSpace::Srgb);
			if(result) result = upload(material.occlusionImageId, material.occlusionImageData, material.occlusionSampler,
				gpu.occlusionTexture, gpu.occlusionRef, gpu.occlusionSamplerRef, TextureColorSpace::Linear);
			if(!result) return result;
		}
		initializationProfile_.texturesMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - geometryDone).count();
		for(size_t i = 0; i < model_.GetMaterials().size() && i < materialResources_.size(); ++i) {
			const auto& material = model_.GetMaterials()[i]; const auto& gpu = materialResources_[i];
			opticalMaterials_.push_back({material.baseColorFactor, material.optics, gpu.textureRef, gpu.samplerRef,
				{material.metallicFactor, material.roughnessFactor, material.normalScale,0}, gpu.normalRef, gpu.normalSamplerRef,
				gpu.metallicRoughnessRef, gpu.metallicRoughnessSamplerRef,
				{material.emissiveFactor[0],material.emissiveFactor[1],material.emissiveFactor[2],material.occlusionStrength},
				gpu.emissiveRef,gpu.emissiveSamplerRef,gpu.occlusionRef,gpu.occlusionSamplerRef});
		}
		meshletResources_.resize(model_.GetMeshCount()); meshletAttempted_.resize(model_.GetMeshCount());
		return {};
	}
	Result<void> PrimitiveRenderer::PrepareModel(const GraphicsRendererInitializationContext& context,
		std::shared_ptr<const RendererModelResources> reuse, std::stop_token cancellation) {
		initializationProfile_ = {}; resources_ = &context.resources;
		meshShadersSupported_ = resources_->GetCapabilities().SupportsMeshShaders(); shaderDirectory_ = context.shaderDirectory;
		const auto start = std::chrono::steady_clock::now();
		auto result = BuildModelResources(context, reuse.get(), cancellation);
		if(result && reuse && reuse->meshletsEnabled) result = EnsureMeshlets(reuse.get(), cancellation, false);
		initializationProfile_.totalMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		return result;
	}
	Result<void> PrimitiveRenderer::EnsureMeshlets(const RendererModelResources* reuse, std::stop_token cancellation, bool createPipeline) {
		if(!meshShadersSupported_ || meshShaderFailed_ || !modelAsset_) return {};
		if(createPipeline && !meshPipeline_.IsInitialized()) {
			const auto start = std::chrono::steady_clock::now();
			auto created = resources_->CreateMeshShaderPipeline(meshPipeline_, shaderDirectory_);
			initializationProfile_.meshletPipelineMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
			if(!created) { meshShaderFailed_ = true; return created; }
		}
		for(uint32_t i = 0; i < model_.GetMeshCount(); ++i) {
			if(cancellation.stop_requested()) return std::unexpected(Error(ErrorCategory::Resource, 132, "Asset preparation cancelled."));
			const auto& mesh = modelAsset_->meshes[i];
			if(mesh.skinIndex != MeshAssetData::kNoSkin || meshletAttempted_[i]) continue;
			meshletAttempted_[i] = true;
			if(reuse) if(auto found = reuse->meshlets.find(mesh.resourceId); found != reuse->meshlets.end() &&
				found->second->GetGeometryGeneration() == model_.GetMesh(i)->GetGeometryGeneration()) {
				meshletResources_[i] = found->second; continue;
			}
			auto prepared = BuildLightmapGeometry(mesh);
			if(!prepared) { NEXUS_LOG_WARNING("Meshlet", prepared.error().GetMessageText()); continue; }
			auto data = MeshletBuilder{}.Build(*prepared);
			if(!data) { NEXUS_LOG_WARNING("Meshlet", data.error().GetMessageText()); continue; }
			auto resource = std::make_shared<MeshletResource>();
			auto uploaded = resources_->CreateMeshletResource(*resource, *data, *model_.GetMesh(i));
			meshletResources_[i] = std::move(resource);
			if(!uploaded) NEXUS_LOG_WARNING("Meshlet", uploaded.error().GetMessageText());
		}
		return {};
	}
	void PrimitiveRenderer::CommitPreparedModel(PrimitiveRenderer& prepared) {
		model_.Swap(prepared.model_); materialResources_.swap(prepared.materialResources_);
		modelTextures_.swap(prepared.modelTextures_); materialSamplers_.swap(prepared.materialSamplers_);
		meshletResources_.swap(prepared.meshletResources_); meshletAttempted_.swap(prepared.meshletAttempted_);
		opticalMaterials_.swap(prepared.opticalMaterials_);
		std::swap(modelAsset_, prepared.modelAsset_); std::swap(modelPose_, prepared.modelPose_);
		initializationProfile_ = prepared.initializationProfile_;
		selectionDraws_.clear(); shadowDraws_.clear(); opticalDraws_.clear();
		std::vector<uint64_t> generations;
		for(const auto& mesh : model_.meshes_) generations.push_back(mesh->GetGeometryGeneration());
		shadows_.RetainGeometry(generations); optics_.RetainGeometry(generations);
		bakedLighting_.Shutdown(); bakedLighting_.Initialize(*resources_);
	}
	void PrimitiveRenderer::ReuseViewResources(PrimitiveRenderer& previous) noexcept {
		environment_.Swap(previous.environment_);
		shadows_.Swap(previous.shadows_);
		optics_.Swap(previous.optics_);
		meshletSettings_ = previous.meshletSettings_;
	}
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
		initializationProfile_ = {};
		const auto initializationStart = std::chrono::steady_clock::now();
		auto stageStart = initializationStart;
		const auto mark = [&](double& milliseconds) {
			const auto now = std::chrono::steady_clock::now();
			milliseconds = std::chrono::duration<double, std::milli>(now - stageStart).count();
			stageStart = now;
		};
		resources_ = &context.resources;
		shaderDirectory_ = context.shaderDirectory;
		meshShadersSupported_ = resources_->GetCapabilities().SupportsMeshShaders();
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

		initializationProfile_.shadersMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - initializationStart).count();
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

		mark(initializationProfile_.pipelineMs);
		initializationProfile_.pipelineCreationMs = initializationProfile_.pipelineMs - initializationProfile_.shadersMs;
		// モデルの選択・CPUロード・再生設定はScene側で済ませる。ここではGPU転送だけを担当する。
        result = selectionOutline_.Initialize(context.resources, context.shaderDirectory);
        if(!result) return result;
		mark(initializationProfile_.selectionMs);

        if(modelAsset_ == nullptr || modelPose_ == nullptr || modelPose_->GetAsset() != modelAsset_) {
			Shutdown();
			return std::unexpected(Error(ErrorCategory::Resource, 1, "Scene model input is not connected."));
		}
		auto built = BuildModelResources(context, initializationReuse_.get(), {});
		if(!built) { Shutdown(); return built; }
		stageStart = std::chrono::steady_clock::now();
        auto initialBuffer = std::make_unique<ConstantBuffer>();
        result = resources_->CreateConstantBuffer(*initialBuffer, sizeof(PrimitiveDrawData));
        if(!result) { Shutdown(); return result; }
        drawBuffers_.push_back(std::move(initialBuffer));
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
		mark(initializationProfile_.viewSystemsMs);
		if(meshletSettings_.enabled) {
			auto meshlets = EnsureMeshlets(nullptr, {}, true);
			if(!meshlets) NEXUS_LOG_WARNING("Meshlet", meshlets.error().GetMessageText());
		}
		mark(initializationProfile_.meshletsMs);
		initializationProfile_.totalMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - initializationStart).count();
		NEXUS_LOG_INFO("Assets.Profile", "Renderer initialization: meshes=" + std::to_string(initializationProfile_.meshes) +
			", vertices=" + std::to_string(initializationProfile_.vertices) + ", materials=" + std::to_string(initializationProfile_.materials) +
			", encodedTextureMiB=" + std::to_string(double(initializationProfile_.encodedTextureBytes) / (1024 * 1024)) +
			", pipeline=" + std::to_string(initializationProfile_.pipelineMs) + ", selection=" + std::to_string(initializationProfile_.selectionMs) +
			", geometry=" + std::to_string(initializationProfile_.geometryMs) + ", textures=" + std::to_string(initializationProfile_.texturesMs) +
			", viewSystems=" + std::to_string(initializationProfile_.viewSystemsMs) + ", meshlets=" + std::to_string(initializationProfile_.meshletsMs) +
			", shaders=" + std::to_string(initializationProfile_.shadersMs) + ", pipelineCreation=" + std::to_string(initializationProfile_.pipelineCreationMs) +
			", meshletPipeline=" + std::to_string(initializationProfile_.meshletPipelineMs) + ", total=" + std::to_string(initializationProfile_.totalMs) + " ms.");
		return {};
	}

	void PrimitiveRenderer::Shutdown() noexcept {
		meshShadersSupported_ = false;
        selectionDraws_.clear();
        selectionOutline_.Shutdown();
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
		drawBuffers_.clear();

		materialResources_.clear();
        materialSamplers_.clear();
		modelTextures_.clear();
		meshletAttempted_.clear();
		// PipelineとBufferはいずれもDevice依存なので、GraphicsSystemがDeviceより先に呼び出す。

		model_.Shutdown();
		pipeline_.Shutdown();
		resources_ = nullptr;
	}

	void PrimitiveRenderer::Render(GraphicsContext& context) {
		if(meshletSettings_.enabled && AreMeshShadersAvailable()) {
			auto meshlets = EnsureMeshlets(nullptr, {}, true);
			if(!meshlets) NEXUS_LOG_WARNING("Meshlet", meshlets.error().GetMessageText());
		}
		optics_.ClearFrameOutputs();
		drawData_.diffuseGIIndex=0;
		drawData_.ddgi={};
		const uint32_t frameIndex = context.GetFrameIndex();
		if(drawBuffers_.empty() ||
		   frameIndex >= drawBuffers_.front()->GetFrameCount()) {
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
		// GI用の強い空を、GIオフ時に未遮蔽の環境光としてそのまま注入しない。
		const bool giRequested = renderScene_->ddgi.enabled || renderScene_->bakedLighting.enabled || renderScene_->globalIllumination.enabled;
		const float fallbackIntensity = giRequested ? 1.0f :
			(std::isfinite(renderScene_->fallbackEnvironmentIntensity) ? std::clamp(renderScene_->fallbackEnvironmentIntensity, 0.0f, 1.0f) : 0.0f);
		auto ambientIrradiance = renderScene_->ambientIrradiance;
		for(auto& channel : ambientIrradiance) channel *= fallbackIntensity;
		drawData_.environment.parameters[0] *= fallbackIntensity;
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
		for(size_t i = 0; i < 3; ++i) drawData_.ambientIrradiance[i] = ambientIrradiance[i];
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
                if(nodeIndex < object.nodeBegin || nodeIndex >= object.nodeEnd) continue;
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
					if(!dynamicGI || !skinned) opticalDraws_.push_back({draw, object.tint, object.material, object.materialBegin});
				}
			}
		}
		auto radiance = (renderScene_->environment.debugMode == LightingDebugMode::Final || renderScene_->environment.debugMode == LightingDebugMode::Indirect || realtimeGI.enabled || renderScene_->ddgi.enabled)
			? optics_.Render(context, projection, renderScene_->directionalLight, opticalDraws_, opticalMaterials_, ambientIrradiance, drawData_.environment, realtimeGI, bakedData, renderScene_->ddgi)
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
        // Entity / Node / Mesh / Submeshの各Drawに独立したCBを割り当てる。
        size_t drawIndex = 0;
		// Rendererは描画意図だけを記述し、Native Command List操作はGraphicsContextへ委譲する。
		context.SetGraphicsPipeline(pipeline_);
		bool meshPipelineBound = false; //< Mesh -> Traditional の切替時だけ PSO を復元する
        selectionDraws_.clear();
		context.SetPrimitiveTopology(PrimitiveTopology::TriangleList);
		const auto& nodeTransforms = modelPose_->GetNodeWorldTransforms();
		for(size_t objectIndex = 0; objectIndex < objectCount; ++objectIndex) {
			const Matrix4x4 instanceWorld = renderScene_->primitives[objectIndex].worldMatrix;
			for(std::size_t nodeIndex = 0; nodeIndex < model_.GetNodes().size(); ++nodeIndex) {
				const auto selectedNode = renderScene_->primitives[objectIndex].nodeIndex;
				if(selectedNode != PrimitiveRenderItem::kAllNodes && selectedNode != nodeIndex) continue;
                if(nodeIndex < renderScene_->primitives[objectIndex].nodeBegin || nodeIndex >= renderScene_->primitives[objectIndex].nodeEnd) continue;
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
                        drawData_.padding = 0;
						drawData_.samplerIndex				   = gpuMaterial.samplerRef.index;
						drawData_.normalTextureIndex = gpuMaterial.normalRef.index;
						drawData_.normalSamplerIndex = gpuMaterial.normalSamplerRef.index;
						drawData_.metallicRoughnessTextureIndex = gpuMaterial.metallicRoughnessRef.index;
						drawData_.metallicRoughnessSamplerIndex = gpuMaterial.metallicRoughnessSamplerRef.index;
						drawData_.pbrParameters = {material.metallicFactor, material.roughnessFactor, material.normalScale, 0};
						drawData_.emissiveOcclusion = {material.emissiveFactor[0], material.emissiveFactor[1], material.emissiveFactor[2], material.occlusionStrength};
						drawData_.extraMaps = {gpuMaterial.emissiveRef.index, gpuMaterial.emissiveSamplerRef.index, gpuMaterial.occlusionRef.index, gpuMaterial.occlusionSamplerRef.index};
						drawData_.tint						   = material.baseColorFactor;
                        // 値だけを既存Frame CBへ渡す。Slider操作でモデル再UploadやDescriptor再生成を行わない。
                        if(object.material.AppliesTo(submesh.materialSlot, object.materialBegin))
                            object.material.Apply(drawData_.tint, drawData_.pbrParameters, drawData_.emissiveOcclusion);
						if(renderScene_ != nullptr) {
							for(size_t channel = 0; channel < 4; ++channel) drawData_.tint[channel] *= renderScene_->primitives[objectIndex].tint[channel];
						}

						const auto		drawDataBytes = std::as_bytes(std::span(&drawData_, 1));
						if(drawIndex == drawBuffers_.size()) {
                            auto buffer = std::make_unique<ConstantBuffer>();
                            auto created = resources_->CreateConstantBuffer(*buffer, sizeof(PrimitiveDrawData));
                            if(!created) { NEXUS_LOG_ERROR("Graphics", created.error().GetMessageText()); return; }
                            drawBuffers_.push_back(std::move(buffer));
                        }
                        ConstantBuffer& drawBuffer = *drawBuffers_[drawIndex++];
						auto			writeResult	  = drawBuffer.Write(frameIndex,
																		 {reinterpret_cast<const uint8_t*>(drawDataBytes.data()), drawDataBytes.size()});
						if(!writeResult) {
							NEXUS_LOG_ERROR("Graphics", writeResult.error().GetMessageText());
							continue;
						}
                        if(object.selected) selectionDraws_.push_back({mesh, &drawBuffer, submesh});
						////////////////////////////////////////////////////////////////////////////////
						// 上位 Renderer は DispatchMesh API を知らず、描画意図だけを Context へ渡す。
						// 同じ独立 Frame CB を Material/Camera 入力に使用し、追加の毎Frame Resource を作らない。
						////////////////////////////////////////////////////////////////////////////////
						if(!hasSkin && meshIndex < meshletResources_.size() && meshletResources_[meshIndex] &&
						   context.DispatchMeshlets(meshPipeline_, *meshletResources_[meshIndex], *mesh, drawBuffer,
						       MeshCullingView{drawData_.worldViewProjection}, static_cast<uint32_t>(submeshIndex), meshletSettings_)) {
							meshPipelineBound = true;
						} else {
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
        auto outlined = selectionOutline_.Render(context, selectionDraws_);
        if(!outlined) NEXUS_LOG_ERROR("Selection", outlined.error().GetMessageText());
	}

} // namespace NexusEngine
