#include "EditorApplication.h"
#include "Graphics/Model/ModelLoader.h"
#include "Graphics/Model/ModelThumbnail.h"

#include "Graphics/Shadow/ShadowSystem.h"
#include "Graphics/RayTracing/OpticalSystem.h"
#include "Graphics/Renderer/PrimitiveRenderer.h"
#include "Graphics/Renderer/GraphicsSystem.h"
#include "Samples/OpticalDemoScene.h"
#include "Samples/StartupAssets.h"
#include "Foundation/Logging/Logger.h"
#include "Runtime/Scene/Components/CameraComponent.h"
#include "Foundation/Utility/String/ConvertString.h"
#include "Graphics/Color/ColorSpace.h"
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <chrono>
#include <tuple>
#include <cctype>

#include "Runtime/Scene/Components/DirectionalLightComponent.h"
#include "ThirdParty/imgui/imgui.h"
#include "Editor/ImGui/ImGuiStyle.h"
#include "Foundation/Reflection/ReinstancingService.h"

namespace NexusEngine {
	Result<void> EditorApplication::PrepareAssets(std::vector<ModelAssetLibrary::Request> requests) {
		// 空のEntityや組み込み形状の未使用ファイル名は、GPUアセット集合を変えない。
		std::erase_if(requests, [](const auto& request) { return request.shape == PrimitiveShape::Asset && request.mesh.empty(); });
		for(auto& request : requests) if(request.shape != PrimitiveShape::Asset) request.mesh.clear();
		std::ranges::sort(requests, [](const auto& a, const auto& b) {
			return std::tie(a.shape, a.mesh, a.texture) < std::tie(b.shape, b.mesh, b.texture);
		});
		requests.erase(std::unique(requests.begin(), requests.end()), requests.end());
		if(assetRequestRevision_ == 0 || requestedAssets_ != requests) {
			assetBuildCancellation_.request_stop();
			requestedAssets_ = requests; ++assetRequestRevision_;
		}
		if(modelLibrary_->MatchesRequests(requests)) {
			resolvedAssetRequestRevision_ = assetRequestRevision_;
			session_.Diagnostics().Resolve("assets");
		}
		if(assetBuild_.valid() && assetBuild_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
			Result<std::unique_ptr<PreparedAssets>> built;
			try { built = assetBuild_.get(); }
			catch(const std::exception& exception) { built = std::unexpected(Error(ErrorCategory::Resource, 131, exception.what())); }
			if(buildingAssetRevision_ == assetRequestRevision_ && resolvedAssetRequestRevision_ != assetRequestRevision_) {
				if(!built) {
					failedAssetRequestRevision_ = assetRequestRevision_;
					session_.Diagnostics().Set("assets", built.error().GetMessageText(), true);
					NEXUS_LOG_ERROR("Assets", built.error().GetMessageText());
				} else {
					auto& prepared = **built;
					const auto publicationStart = std::chrono::steady_clock::now();
					if(prepared.renderer) {
						auto idle = graphicsSystem_->WaitForIdle(); if(!idle) return idle;
						auto previousAsset = modelAsset_;
						modelPose_ = std::move(prepared.pose);
						materialRenderer_->CommitPreparedModel(*prepared.renderer);
						modelAsset_ = prepared.asset;
						materialRenderer_->SetModelInput(modelAsset_.get(), &modelPose_);
						if(prepared.preview) {
							if(cameraPreviewRenderer_) {
								cameraPreviewRenderer_->CommitPreparedModel(*prepared.preview);
								cameraPreviewRenderer_->SetModelInput(modelAsset_.get(), &modelPose_);
							} else {
								prepared.preview->SetModelInput(modelAsset_.get(), &modelPose_);
								prepared.preview->SetRenderScene(&cameraPreviewScene_);
								auto adopted = graphicsSystem_->AdoptPreparedCameraPreviewRenderer(prepared.preview.get());
								if(!adopted) return adopted;
								cameraPreviewRenderer_ = std::move(prepared.preview);
							}
						} else {
							auto released = graphicsSystem_->ReleaseCameraPreviewRenderer(); if(!released) return released;
							cameraPreviewRenderer_.reset();
						}
						previewInitializationFailed_ = false; editPose_.reset();
						mirrorMaterialSlot_ = glassMaterialSlot_ = UINT32_MAX;
						for(uint32_t slot = 0; slot < modelAsset_->materials.size(); ++slot) {
							if(modelAsset_->materials[slot].name == "Mirror Sphere") mirrorMaterialSlot_ = slot;
							if(modelAsset_->materials[slot].name == "Glass Sphere") glassMaterialSlot_ = slot;
						}
						if(workspace_) workspace_->ViewportEditor().SetModel(*modelAsset_, modelPose_);
						++renderScene_.ddgi.revision;
					}
					modelLibrary_ = std::move(prepared.library);
					uploadedAssetRevision_ = modelLibrary_->Revision();
					if(cameraPreviewRenderer_) previewAssetRevision_ = uploadedAssetRevision_;
					resolvedAssetRequestRevision_ = assetRequestRevision_;
					session_.Diagnostics().Resolve("assets");
					const auto& profile = materialRenderer_->GetInitializationProfile();
					const double published = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - publicationStart).count();
					NEXUS_LOG_INFO("Assets", "Asset update: CPU=" + std::to_string(prepared.loadMilliseconds) +
						" ms, resources=" + std::to_string(prepared.rendererMilliseconds) + " ms, publish=" + std::to_string(published) +
						" ms; meshes uploaded/reused=" + std::to_string(profile.meshesUploaded) + "/" + std::to_string(profile.meshesReused) +
						", textures created/reused=" + std::to_string(profile.texturesCreated) + "/" + std::to_string(profile.texturesReused));
				}
			}
		}
		if(!assetBuild_.valid() && resolvedAssetRequestRevision_ != assetRequestRevision_ && failedAssetRequestRevision_ != assetRequestRevision_) {
			auto library = std::make_unique<ModelAssetLibrary>(*modelLibrary_);
			const bool withPreview = workspace_ && session_.GetCameraPreview().IsValid() && workspace_->Viewport().IsCameraPreviewVisible();
			const bool reusePreview = cameraPreviewRenderer_ != nullptr;
			auto reuse = materialRenderer_->CaptureModelResources();
			assetBuildCancellation_ = std::stop_source{};
			buildingAssetRevision_ = assetRequestRevision_;
			assetBuild_ = std::async(std::launch::async,
				[library = std::move(library), requests = std::move(requests), reuse = std::move(reuse), current = modelAsset_,
				 graphics = graphicsSystem_, withPreview, reusePreview, cancellation = assetBuildCancellation_.get_token()]() mutable -> Result<std::unique_ptr<PreparedAssets>> {
					auto prepared = std::make_unique<PreparedAssets>();
					const auto start = std::chrono::steady_clock::now();
					library->Retain(requests);
					for(const auto& request : requests) {
						if(cancellation.stop_requested()) return std::unexpected(Error(ErrorCategory::Resource,132,"Asset preparation cancelled."));
						auto range = library->Resolve(request.shape, request.mesh, request.texture);
						if(!range) return std::unexpected(std::move(range.error()));
					}
					prepared->asset = library->SharedSnapshot(); prepared->library = std::move(library);
					if(prepared->asset == current) return prepared;
					auto pose = prepared->pose.Initialize(*prepared->asset); if(!pose) return std::unexpected(std::move(pose.error()));
					const auto loaded = std::chrono::steady_clock::now();
					prepared->loadMilliseconds = std::chrono::duration<double, std::milli>(loaded - start).count();
					prepared->renderer = std::make_unique<PrimitiveRenderer>();
					prepared->renderer->SetModelInput(prepared->asset.get(), &prepared->pose);
					auto initialized = graphics->PrepareModelResources(prepared->renderer.get(), std::move(reuse), cancellation);
					if(!initialized) return std::unexpected(std::move(initialized.error()));
					if(withPreview) {
						if(cancellation.stop_requested()) return std::unexpected(Error(ErrorCategory::Resource,132,"Asset preparation cancelled."));
						prepared->preview = std::make_unique<PrimitiveRenderer>();
						prepared->preview->SetModelInput(prepared->asset.get(), &prepared->pose);
						auto resources = prepared->renderer->CaptureModelResources();
						initialized = reusePreview
							? graphics->PrepareModelResources(prepared->preview.get(), std::move(resources), cancellation)
							: graphics->PrepareSharedRenderer(prepared->preview.get(), std::move(resources));
						if(!initialized) return std::unexpected(std::move(initialized.error()));
					}
					prepared->rendererMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - loaded).count();
					return prepared;
				});
		}
		if(assetBuild_.valid() && resolvedAssetRequestRevision_ != assetRequestRevision_) session_.Diagnostics().Set("assets.loading", "Loading mesh assets...", false);
		else session_.Diagnostics().Resolve("assets.loading");
		return {};
	}

	Result<void> EditorApplication::PrepareCameraPreview(bool requested) {
		if(previewBuild_.valid() && previewBuild_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
			Result<std::unique_ptr<PreparedAssets>> built;
			try { built = previewBuild_.get(); }
			catch(const std::exception& exception) { built = std::unexpected(Error(ErrorCategory::Resource, 131, exception.what())); }
			if(requested && buildingPreviewAssetRevision_ == uploadedAssetRevision_) {
				previewAssetRevision_ = uploadedAssetRevision_;
				if(!built) {
					previewInitializationFailed_ = true;
					session_.Diagnostics().Set("preview.renderer", built.error().GetMessageText(), true);
				} else if((**built).asset == modelAsset_) {
					auto& renderer = (**built).preview;
					renderer->SetModelInput(modelAsset_.get(), &modelPose_);
					renderer->SetRenderScene(&cameraPreviewScene_);
					auto idle = graphicsSystem_->WaitForIdle(); if(!idle) return idle;
					if(cameraPreviewRenderer_) {
						cameraPreviewRenderer_->CommitPreparedModel(*renderer);
						cameraPreviewRenderer_->SetModelInput(modelAsset_.get(), &modelPose_);
						cameraPreviewRenderer_->SetRenderScene(&cameraPreviewScene_);
					} else {
						auto adopted = graphicsSystem_->AdoptPreparedCameraPreviewRenderer(renderer.get()); if(!adopted) return adopted;
						cameraPreviewRenderer_ = std::move(renderer);
					}
					previewInitializationFailed_ = false;
					session_.Diagnostics().Resolve("preview.renderer");
				}
			}
		}
		if(requested && !previewBuild_.valid() && !assetBuild_.valid() && !previewInitializationFailed_ &&
			(!cameraPreviewRenderer_ || previewAssetRevision_ != uploadedAssetRevision_)) {
			buildingPreviewAssetRevision_ = uploadedAssetRevision_;
			previewBuild_ = std::async(std::launch::async, [asset = modelAsset_, reuse = materialRenderer_->CaptureModelResources(), graphics = graphicsSystem_]() -> Result<std::unique_ptr<PreparedAssets>> {
				auto prepared = std::make_unique<PreparedAssets>();
				prepared->asset = asset;
				auto initialized = prepared->pose.Initialize(*asset);
				if(!initialized) return std::unexpected(std::move(initialized.error()));
				prepared->preview = std::make_unique<PrimitiveRenderer>();
				prepared->preview->SetModelInput(asset.get(), &prepared->pose);
				initialized = graphics->PrepareSharedRenderer(prepared->preview.get(), std::move(reuse));
				if(!initialized) return std::unexpected(std::move(initialized.error()));
				return prepared;
			});
		}
		return {};
	}
	void EditorApplication::FinishAssetPreparation() noexcept {
		assetBuildCancellation_.request_stop();
		// DeviceとFactoryの解放、またはコードの差し替えより先に準備処理を終了する。
		for(auto* future : {&assetBuild_, &previewBuild_}) {
			try { if(future->valid()) { auto prepared = future->get(); } }
			catch(const std::exception& exception) { NEXUS_LOG_ERROR("Assets", exception.what()); }
		}
		try { if(thumbnailBuild_.valid()) { auto image = thumbnailBuild_.get(); } }
		catch(const std::exception& exception) { NEXUS_LOG_ERROR("Assets", exception.what()); }
	}

	ImTextureRef EditorApplication::RequestAssetPreview(const std::string& key, const std::string& reference, bool texture, int node) {
		auto cached = imguiRenderer_.FindPreview(key);
		if(cached._TexData || !imguiRenderer_.CanCreatePreview()) return cached;
		if(thumbnailBuild_.valid()) {
			if(thumbnailBuild_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return {};
			try {
				auto image = thumbnailBuild_.get();
				if(image) imguiRenderer_.CreatePreview(thumbnailBuildKey_, *image);
				else failedThumbnails_.insert(thumbnailBuildKey_);
			} catch(const std::exception&) { failedThumbnails_.insert(thumbnailBuildKey_); }
			return imguiRenderer_.FindPreview(key);
		}
		// 本体の読み込みを優先し、大きいモデルをサムネイル用に同時ロードしない。
		if(assetBuild_.valid() || failedThumbnails_.contains(key)) return {};
		if(failedThumbnails_.size() >= 128) failedThumbnails_.clear();
		thumbnailBuildKey_ = key;
		auto library = std::make_unique<ModelAssetLibrary>(*modelLibrary_);
		thumbnailBuild_ = std::async(std::launch::async, [library = std::move(library), reference, texture, node]() -> Result<DecodedImage> {
			if(texture) {
				auto bytes = library->Texture(reference);
				if(!bytes) return std::unexpected(std::move(bytes.error()));
				auto image = WicImageDecoder{}.Decode(**bytes);
				if(!image) return std::unexpected(std::move(image.error()));
				return CreateImageThumbnail(*image);
			}
			auto model = library->Model(reference);
			if(!model) return std::unexpected(std::move(model.error()));
			return CreateModelThumbnail(**model, node);
		});
		return {};
	}

	Result<void> EditorApplication::CreateScene(const std::filesystem::path& assetDirectory, const std::filesystem::path& startupLevel) {
		editPose_.reset();
        reportedAssetErrors_.clear();
		// Sceneが使用するモデルとClipを選択する。GPU初期化前にもCPUだけで構築できる。
		modelPose_.Shutdown();
		session_.Close();

		// モデル読み込み
		OpticalSphereMaterialSlots sphereMaterials;
		auto asset = LoadSampleStartupAsset(assetDirectory, &sphereMaterials);
		if(!asset) {
			// 読み込みに失敗したらエラーを返す
			return std::unexpected(std::move(asset.error()));
		}

		// モデルアセットに所有権を移動
		auto startupAsset = std::make_shared<ModelAssetData>(std::move(*asset));
		modelAsset_ = startupAsset;
		mirrorMaterialSlot_ = sphereMaterials.mirror;
		glassMaterialSlot_ = sphereMaterials.glass;
		auto poseResult = modelPose_.Initialize(*modelAsset_);
        modelLibrary_ = std::make_unique<ModelAssetLibrary>(std::move(startupAsset), assetDirectory,
            [assetDirectory] { return LoadSampleStartupAsset(assetDirectory); });
        uploadedAssetRevision_ = modelLibrary_->Revision();
		if(!poseResult) return poseResult;

		if(!modelAsset_->animations.empty()) {
			poseResult = modelPose_.Play(0, true);
			if(!poseResult) {
				return poseResult;
			}
		}
		EditorSettingsStore bootSettings;
		bootSettings.Load(std::filesystem::current_path());
		auto levelResult = session_.Open(startupLevel.empty()
			? std::filesystem::path{ConvertString(bootSettings.project.startupLevel)} : startupLevel);
		if(!levelResult) return levelResult;
		renderScene_.ambientIrradiance = {1.5f, 1.8f, 2.4f}; // HDR未指定時の遠方環境の照度
		renderScene_.fallbackEnvironmentIntensity = .05f; // GIオフ時は弱い環境光だけを残す
		renderScene_.globalIllumination.enabled = false;
		renderScene_.bakedLighting.enabled = false;
		renderScene_.ddgi.enabled = bootSettings.editor.giMode == "DDGI";
		renderScene_.bakedLighting.cachePath = "Generated/Lighting/DemoLevel.nxgi";
		constexpr char bakedPath[] = "Generated/Lighting/DemoLevel.nxgi";
		std::copy_n(bakedPath, sizeof(bakedPath), bakedPath_.begin());
		renderScene_.fog.enabled = true;
		return session_.GetLevel()->Update(0.0f);
	}

	void EditorApplication::ShowGui() {
		if(workspace_) workspace_->Draw();
		if(showLegacyDiagnostics_) ShowLegacyDiagnostics();
	}

	void EditorApplication::ShowLegacyDiagnostics() {
		// フレームが開始していない場合は終了
		if(!guiContext_.IsFrameActive()) {
			return;
		}
		if(ImGui::Begin("Distance Fog")) {
			auto settings = renderScene_.fog;
			bool changed = ImGui::Checkbox("Fog Enabled", &settings.enabled);
			changed |= ImGui::DragFloat("Start Distance", &settings.startDistance, 0.5f, 0, 10000);
			changed |= ImGui::DragFloat("End Distance", &settings.endDistance, 0.5f, 0.01f, 10000);
			auto displayColor = ColorSpace::LinearToSrgb(settings.color);
			if(ImGui::ColorEdit3("Fog Color (sRGB)", displayColor.data())) {
				settings.color = ColorSpace::SrgbToLinear(displayColor); changed = true;
			}
			if(changed && settings.IsValid()) renderScene_.fog = settings;
			if(!settings.IsValid()) ImGui::TextUnformatted("End distance must be greater than start distance.");
			if(ImGui::Button("Reset Fog")) { renderScene_.fog = {}; renderScene_.fog.enabled = true; }
		}
		ImGui::End();
		if(graphicsSystem_) {
			if(ImGui::Begin("Post Processing")) {
				auto settings = graphicsSystem_->GetPostProcessSettings();
				int mapper = static_cast<int>(settings.GetEffectiveToneMapper());
				bool changed = ImGui::Combo("Tone Mapping", &mapper, "Linear Clamp\0ACES Fitted\0PBR Neutral\0");
				if(changed) { settings.toneMapper = static_cast<ToneMapper>(mapper); settings.filmicEnabled = true; }
				changed |= ImGui::Checkbox("Anti-aliasing (FXAA)", &settings.antiAliasingEnabled);
				changed |= ImGui::SliderFloat("Exposure (EV)", &settings.exposureEV, -10, 10);
				changed |= ImGui::Checkbox("Bloom", &settings.bloomEnabled);
				changed |= ImGui::SliderFloat("Bloom Intensity", &settings.bloomIntensity, 0, 2);
				changed |= ImGui::DragFloat("Bloom Threshold (linear)", &settings.bloomThreshold, 0.05f, 0, 100);
				changed |= ImGui::SliderFloat("Bloom Soft Knee", &settings.bloomSoftKnee, 0, 1);
				changed |= ImGui::DragFloat3("White Balance RGB", settings.whiteBalance.data(), 0.01f, 0.01f, 16);
				changed |= ImGui::SliderFloat("Saturation", &settings.saturation, 0, 4);
				changed |= ImGui::SliderFloat("Contrast", &settings.contrast, 0.01f, 4);
				changed |= ImGui::SliderFloat("Grading Gamma", &settings.gamma, 0.01f, 4);
				changed |= ImGui::DragFloat3("Gain", settings.gain.data(), 0.01f, 0, 16);
				changed |= ImGui::DragFloat3("Offset", settings.offset.data(), 0.001f, -16, 16);
				if(ImGui::Button("Reset Post Processing")) { settings = {}; changed = true; }
				if(changed) {
					auto result = graphicsSystem_->SetPostProcessSettings(settings);
					if(!result) NEXUS_LOG_ERROR("Editor", result.error().GetMessageText());
				}
			}
			ImGui::End();
		}
		// UI は設定を操作するだけ。AS/MS や Resource を Editor から生成しない。
		if(materialRenderer_) {
			if(ImGui::Begin("Mesh Shader")) {
				const bool available = materialRenderer_->AreMeshShadersAvailable();
				ImGui::TextUnformatted(available ? "Mesh Shader Tier 1 / Pipeline ready" : "Mesh Shader unavailable / Traditional rendering");
				ImGui::BeginDisabled(!available);
				auto& settings = materialRenderer_->GetMeshletSettings();
				ImGui::Checkbox("Use Mesh Shader", &settings.enabled);
				ImGui::Checkbox("Enable Meshlet Culling", &settings.enableCulling);
				int mode = static_cast<int>(settings.visualization);
				if(ImGui::Combo("Debug Visualization", &mode, "Normal\0Meshlet Color\0Culling Result\0"))
					settings.visualization = static_cast<MeshletVisualization>(mode);
				ImGui::TextWrapped("Culling Result renders all candidates: green = visible, red = outside. Outside geometry may still be clipped by the rasterizer.");
				ImGui::EndDisabled();
			}
			ImGui::End();
		}
		if(optics_ && ImGui::Begin("Ray Traced Optics")) {
			ImGui::TextUnformatted("Optical mirrors / glass and ordinary PBR metallic reflections");
			ImGui::TextUnformatted("PBRTestLevel: front row gold, back row dielectric; roughness increases left to right.");
			auto settings = optics_->GetSettings();
			bool changed = ImGui::Checkbox("Reflection / Transmission", &settings.enabled);
			changed |= ImGui::Checkbox("PBR Metallic Reflections", &settings.metallicReflections);
			changed |= ImGui::Checkbox("Reflection Denoising", &settings.reflectionDenoising);
			const auto timings=optics_->GetGpuTimings();
			if(timings.valid) ImGui::Text("GPU (ms): AS %.2f / DDGI %.2f / Reflections %.2f / Filter %.2f",
				timings.milliseconds[0],timings.milliseconds[1],timings.milliseconds[2],timings.milliseconds[3]);
			int reflectionSamples = static_cast<int>(settings.reflectionSamples);
			if(ImGui::SliderInt("Metal Reflection Samples", &reflectionSamples, 1, 64)) {
				settings.reflectionSamples = static_cast<uint32_t>(reflectionSamples); changed = true;
			}
			ImGui::TextUnformatted("Polished: one stable ray. Rough: prefiltered IBL. Local blend: roughness 0.2-0.5.");
			int bounces = static_cast<int>(settings.maxBounces);
			if(ImGui::SliderInt("Max Optical Bounces", &bounces, 1, 12)) {
				settings.maxBounces = static_cast<uint32_t>(bounces);
				changed = true;
			}
			changed |= ImGui::DragFloat("Optical Ray Bias", &settings.rayBias, 0.0001f, 0.00001f, 0.1f, "%.5f");
			if(changed) {
				auto result = optics_->SetSettings(settings);
				if(!result) {
					NEXUS_LOG_WARNING("Optics", result.error().GetMessageText());
				}
			}
			if(materialRenderer_) {
				const auto materials = materialRenderer_->GetOpticalMaterials();
				for(uint32_t slot = 0; slot < materials.size(); ++slot) {
					if(slot != mirrorMaterialSlot_ && slot != glassMaterialSlot_) {
						continue;
					}
					ImGui::PushID(static_cast<int>(slot));
					ImGui::SeparatorText(slot == mirrorMaterialSlot_ ? "Mirror Sphere (Left)" : "Glass Sphere (Right)");
					auto material = materials[slot].optics;
					bool edited = ImGui::SliderFloat("Reflectivity", &material.reflectivity, 0, 1);
					edited |= ImGui::SliderFloat("Transmission", &material.transmission, 0, 1);
					edited |= ImGui::SliderFloat("IOR", &material.indexOfRefraction, 1, 2.5f);
					edited |= ImGui::DragFloat3("RGB Absorption", material.absorption.data(), 0.01f, 0, 3);
					if(edited) {
						auto result = materialRenderer_->SetOpticalMaterial(slot, material);
						if(!result) {
							NEXUS_LOG_WARNING("Optics", result.error().GetMessageText());
						}
					}
					ImGui::PopID();
				}
			}
		}
		if(optics_) {
			ImGui::End();
		}


		if(shadows_ != nullptr) {
			if(ImGui::Begin("Shadows")) {
				const auto capabilities = shadows_->GetCapabilities();
				const auto statistics = shadows_->GetStatistics();
				ImGui::Text("Ray Tracing Supported: %s", capabilities.SupportsRayTracing() ? "Yes" : "No");
				const char* tier = capabilities.rayTracing == RayTracingSupport::Tier1_1 ? "1.1 or higher" :
					(capabilities.rayTracing == RayTracingSupport::Tier1_0 ? "1.0" : "Unsupported");
				ImGui::Text("Ray Tracing Tier: %s", tier);
				const char* technique = statistics.technique == ShadowTechnique::RayTracing ? "RayTracing" :
					(statistics.technique == ShadowTechnique::ShadowMap ? "ShadowMap" : "Off");
				ImGui::Text("Current Shadow Technique: %s", technique);
				ImGui::Text("BLAS Count: %u / TLAS Instances: %u", statistics.blasCount, statistics.tlasInstanceCount);
				ImGui::Text("BLAS Builds: %llu / TLAS Builds: %llu / Updates: %llu",
					static_cast<unsigned long long>(statistics.blasBuildCount),
					static_cast<unsigned long long>(statistics.tlasBuildCount),
					static_cast<unsigned long long>(statistics.tlasUpdateCount));
				auto settings = shadows_->GetSettings();
				bool changed = ImGui::Checkbox("Shadows Enabled", &settings.enabled);
				int mode = static_cast<int>(settings.mode);
				if(ImGui::Combo("Shadow Mode", &mode, "Auto\0RayTracing\0ShadowMap\0")) {
					settings.mode = static_cast<ShadowMode>(mode);
					changed = true;
				}
				changed |= ImGui::DragFloat("Ray Origin Bias", &settings.rayOriginBias, 0.0001f, 0, 1, "%.4f");
				changed |= ImGui::DragFloat("Ray TMin", &settings.rayTMin, 0.0001f, 0, 1, "%.4f");
				changed |= ImGui::Checkbox("Soft Shadow", &settings.softShadow);
				int samples = static_cast<int>(settings.sampleCount);
				if(ImGui::SliderInt("Sample Count", &samples, 1, 64)) {
					settings.sampleCount = static_cast<uint32_t>(samples);
					changed = true;
				}
				changed |= ImGui::DragFloat("Shadow Map Bias", &settings.shadowMapBias, 0.0001f, 0, 0.1f, "%.4f");
				changed |= ImGui::DragFloat("Shadow Map Extent", &settings.shadowMapExtent, 1, 1, 1000);
				if(changed) {
					auto result = shadows_->SetSettings(settings);
					if(!result) {
						NEXUS_LOG_WARNING("Editor", result.error().GetMessageText());
					}
				}
				float ambientIntensity = renderScene_.ambientIrradiance[0] / 0.5f;
				auto& environment = renderScene_.environment;
				ImGui::SeparatorText("Environment / PBR");
				ImGui::InputText("HDR image path", environmentPath_.data(), environmentPath_.size());
				if(ImGui::Button("Load / Reload HDR")) {
					environment.imagePath = std::filesystem::path(reinterpret_cast<const char8_t*>(environmentPath_.data()));
					++environment.revision;
				}
				ImGui::Checkbox("IBL Enabled", &environment.enabled);
				ImGui::DragFloat("IBL Intensity", &environment.intensity, 0.01f, 0, 100);
				ImGui::SliderAngle("Environment Rotation", &environment.rotation, -180, 180);
				ImGui::Checkbox("Legacy Half-Lambert", &environment.legacyHalfLambert);
				auto& gi = renderScene_.globalIllumination;
				auto& baked = renderScene_.bakedLighting;
				auto& ddgi=renderScene_.ddgi;
				ImGui::SeparatorText("DDGI / Dynamic Irradiance Field");
				ImGui::TextUnformatted("GI mode: Edit > Preferences > Rendering");
				ImGui::Checkbox("Freeze DDGI",&ddgi.freeze);
				ImGui::Text("Probe updates: %u",optics_?optics_->GetDDGIFrameCount():0);
				ImGui::TextUnformatted(optics_ && optics_->IsDDGIActive()?"DDGI Active":"DDGI Inactive / requires DXR and valid geometry");
				ImGui::DragFloat3("DDGI Origin",ddgi.origin.data(),.1f);
				ImGui::DragFloat3("DDGI Spacing",ddgi.spacing.data(),.05f,.05f,100);
				int ddgiCounts[3] {static_cast<int>(ddgi.counts[0]),static_cast<int>(ddgi.counts[1]),static_cast<int>(ddgi.counts[2])};
				if(ImGui::SliderInt3("DDGI Counts",ddgiCounts,2,16)) {
					auto edited=ddgi;for(size_t n=0;n<3;++n) edited.counts[n]=static_cast<uint32_t>(ddgiCounts[n]);
					if(edited.IsValid()) ddgi.counts=edited.counts;
				}
				ImGui::TextUnformatted("Maximum: 2048 probes / 256 rays per probe");
				int rayLevel=static_cast<int>(ddgi.raysPerProbe/32)-1;
				if(ImGui::SliderInt("DDGI Rays x32",&rayLevel,0,7)) ddgi.raysPerProbe=static_cast<uint32_t>(rayLevel+1)*32;
				ImGui::SliderFloat("DDGI Hysteresis",&ddgi.hysteresis,0,.995f);
				ImGui::SliderFloat("DDGI Intensity",&ddgi.intensity,0,10);
				ImGui::DragFloat("DDGI Normal Bias",&ddgi.normalBias,.001f,0,1);
				ImGui::DragFloat("DDGI View Bias",&ddgi.viewBias,.001f,0,1);
				ImGui::DragFloat("DDGI Ray Distance",&ddgi.maxDistance,1,1,1000);
				ImGui::Checkbox("DDGI Relocation",&ddgi.relocation);
				ImGui::Checkbox("DDGI Multi-bounce Feedback",&ddgi.feedback);
				ImGui::Checkbox("DDGI Rotate Rays",&ddgi.rotateRays);
				if(ImGui::Button("Reset DDGI History")) ++ddgi.revision;
				ImGui::TextUnformatted("DDGI requires DXR. Fixed rays + history reduce flicker; lighting changes need convergence.");
				ImGui::TextUnformatted("Priority: DDGI > Baked GI > RTGI. Diffuse GI Only displays the selected result.");
				ImGui::SeparatorText("Baked GI / Light Probes");
				ImGui::Text("Baked GI Enabled: %s", baked.enabled ? "Yes" : "No");
				ImGui::InputText("Baked GI Cache",bakedPath_.data(),bakedPath_.size());
				ImGui::TextWrapped("%s", materialRenderer_ ? materialRenderer_->GetBakedLightingStatus().c_str() : "Renderer unavailable");
				ImGui::TextUnformatted("Missing cache uses ambient. Bake explicitly after static scene/light changes.");
				int bakeSamples = static_cast<int>(baked.samples), bakeBounces = static_cast<int>(baked.maxBounces);
				if(ImGui::SliderInt("Bake Samples", &bakeSamples, 16, 4096)) baked.samples = static_cast<uint32_t>(bakeSamples);
				if(ImGui::SliderInt("Bake Bounces", &bakeBounces, 1, 8)) baked.maxBounces = static_cast<uint32_t>(bakeBounces);
				int chart = baked.chartSize == 4 ? 0 : baked.chartSize == 8 ? 1 : 2;
				if(ImGui::Combo("Chart Resolution", &chart, "4\0" "8\0" "16\0")) baked.chartSize = 4U << chart;
				int probeCounts[3] {static_cast<int>(baked.probeCounts[0]),static_cast<int>(baked.probeCounts[1]),static_cast<int>(baked.probeCounts[2])};
				if(ImGui::SliderInt3("Probe Counts", probeCounts, 2, 16)) for(size_t n=0;n<3;++n) baked.probeCounts[n]=static_cast<uint32_t>(probeCounts[n]);
				ImGui::SliderFloat("Baked GI Intensity", &baked.intensity, 0, 10);
				ImGui::Checkbox("Automatic Probe Bounds",&baked.automaticBounds);
				if(!baked.automaticBounds) {
					ImGui::DragFloat3("Probe Minimum",baked.probeMin.data(),.1f);
					ImGui::DragFloat3("Probe Maximum",baked.probeMax.data(),.1f);
				}
				if(ImGui::Button("Bake and Save (CPU)") && materialRenderer_) {
					baked.cachePath=std::filesystem::path(reinterpret_cast<const char8_t*>(bakedPath_.data()));
					// 編集時の明示操作のみCPUベイクする。描画ループからは呼び出さない。
					auto status=materialRenderer_->BakeLighting(baked);
					if(status) ++baked.revision;
					else NEXUS_LOG_ERROR("BakedGI",status.error().GetMessageText());
				}
				ImGui::SameLine();
				if(ImGui::Button("Reload Baked GI")) {
					baked.cachePath=std::filesystem::path(reinterpret_cast<const char8_t*>(bakedPath_.data()));
					++baked.revision;
				}
				ImGui::TextUnformatted("CPU bake pauses editing. Runtime samples saved lighting without GI rays.");
				ImGui::BeginDisabled(baked.enabled || ddgi.enabled);
				ImGui::Text("Diffuse RTGI Enabled: %s", gi.enabled ? "Yes" : "No");
				ImGui::Text("RTGI output (last frame): %s", materialRenderer_ && materialRenderer_->IsGlobalIlluminationActive()
					? "Available" : "Inactive / fallback");
				int giSamples = static_cast<int>(gi.samplesPerPixel), giBounces = static_cast<int>(gi.maxBounces);
				if(ImGui::SliderInt("GI Samples Per Pixel", &giSamples, 1, 64)) gi.samplesPerPixel = static_cast<uint32_t>(giSamples);
				if(ImGui::SliderInt("GI Diffuse Segments", &giBounces, 1, 4)) gi.maxBounces = static_cast<uint32_t>(giBounces);
				ImGui::SliderFloat("GI Intensity", &gi.intensity, 0, 10);
				ImGui::TextUnformatted("Diffuse RTGI replaces ambient / diffuse IBL. DXR static geometry required.");
				ImGui::EndDisabled();
				int lightingDebug = static_cast<int>(environment.debugMode);
				if(ImGui::Combo("Lighting View", &lightingDebug,
					"Final Lighting\0Direct Lighting Only\0Indirect Lighting Only\0Diffuse IBL Only\0Specular IBL Only\0Base Color\0Metallic\0Roughness\0Normal\0AO\0Emissive\0Diffuse GI Only\0"))
					environment.debugMode = static_cast<LightingDebugMode>(lightingDebug);
				if(ImGui::SliderFloat("Fallback Sky Intensity", &ambientIntensity, 0.0f, 10.0f))
					renderScene_.ambientIrradiance = {0.5f * ambientIntensity, 0.6f * ambientIntensity, 0.8f * ambientIntensity};
				if(auto* level = session_.GetLevel(); level && level->GetScene()) {
					level->GetScene()->ForEach<DirectionalLightComponent>([&](Entity entity, DirectionalLightComponent& component) {
						ImGui::PushID(static_cast<int>(entity.GetId().index));
						auto edited = component;
						bool changed = ImGui::Checkbox("Light Enabled", &edited.enabled);
						changed |= ImGui::DragFloat3("Light Direction", edited.light.direction.data(), 0.01f);
						auto displayColor = ColorSpace::LinearToSrgb(edited.light.color);
						if(ImGui::ColorEdit3("Light Color (sRGB)", displayColor.data())) {
							edited.light.color = ColorSpace::SrgbToLinear(displayColor);
							changed = true;
						}
						changed |= ImGui::DragFloat("Angular Radius (rad)", &edited.light.angularRadius, 0.001f, 0, 1.5f);
						changed |= ImGui::DragFloat("Light Intensity", &edited.light.intensity, 0.05f, 0, 100000);
						if(changed && edited.IsValid()) component = edited;
						ImGui::PopID();
					});
				}
			}
			ImGui::End();
		}
	}

	Result<void> EditorApplication::Initialize(const GraphicsRenderExtensionContext& context) {
		if(!context.graphicsSystem)
			return std::unexpected(Error(ErrorCategory::Graphics, 102, "Editor viewport requires GraphicsSystem."));
		auto viewportResult = context.graphicsSystem->EnableSceneViewport();
		if(!viewportResult) return viewportResult;
		// UIはBackBuffer、Sceneは専用RT/Depthへ描くため、UI PSOにDepth形式を要求しない。
		auto uiContext = context;
		uiContext.depthStencilFormat = DepthStencilFormat::None;
		auto rendererResult = imguiRenderer_.Initialize(uiContext);
		if(!rendererResult) {
			session_.Close();
			return rendererResult;
		}

		auto guiResult = guiContext_.Initialize({.textureProvider = &imguiRenderer_});
		if(!guiResult) {
			imguiRenderer_.Shutdown();
			session_.Close();
			return guiResult;
		}

		graphicsSystem_ = context.graphicsSystem;
		auto workspaceResult = CreateWorkspace(static_cast<float>(GetDpiForWindow(static_cast<HWND>(context.nativeWindow))) / 96.0f);
		if(!workspaceResult) { Shutdown(); return workspaceResult; }
		ReinstancingService::Get().Register({"Editor", this,
			[](void* context) { return static_cast<EditorApplication*>(context)->CaptureReinstancing(); },
			[](void* context) noexcept { static_cast<EditorApplication*>(context)->ReleaseReinstancing(); },
			[](void* context, const ReflectionValue& snapshot) { static_cast<EditorApplication*>(context)->RestoreReinstancing(snapshot); }});
		return {};
	}

	Result<void> EditorApplication::CreateWorkspace(float dpiScale) {
		workspace_ = std::make_unique<EditorWorkspace>(session_);
		workspace_->Initialize(std::filesystem::current_path(), editorExtensions_);
		workspace_->SetDpiScale(dpiScale);
		workspace_->ViewportEditor().SetModel(*modelAsset_, modelPose_);
        workspace_->AssetEditor().Configure(modelLibrary_->Root(),
            [this](const std::filesystem::path& path, bool texture) -> Result<std::string> {
                auto reference = modelLibrary_->Reference(path);
                if(!reference) return std::unexpected(reference.error());
                // 選択確定ではパスと形式だけを検証し、重いデコードは準備スレッドで行う。
                auto extension = AssetCatalog::Utf8(path.extension());
                std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                const bool supported = texture
                    ? extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp" || extension == ".tif" || extension == ".tiff"
                    : extension == ".gltf" || extension == ".glb" || extension == ".obj";
                if(!supported) return std::unexpected(Error(ErrorCategory::Resource, 130, "Unsupported asset format."));
                return reference;
            }, [this](const std::string& reference) -> const ModelAssetData* {
                return modelLibrary_->FindModel(reference);
            }, [this](const std::string& path, bool texture, int node) -> ImTextureRef {
                std::string reference;
                if(path == kStartupModelAssetReference && !texture) reference = path;
                else if(!path.empty()) {
                    auto result = modelLibrary_->Reference(std::filesystem::path(reinterpret_cast<const char8_t*>(path.c_str())));
                    if(!result) return {}; reference = *result;
                }
                const auto key = std::string(texture ? "texture:" : "mesh:") + reference + ":" + std::to_string(node);
                return RequestAssetPreview(key, reference, texture, node);
            });
		if(!workspace_->Viewport().AttachLocalPreview(imguiRenderer_, ImGuiRenderer::SceneViewportTexture,
			[graphics = graphicsSystem_](uint32_t width, uint32_t height) { return graphics->RequestSceneViewportSize(width, height); })) {
			return std::unexpected(Error(ErrorCategory::Graphics, 103, "Failed to connect local scene viewport."));
		}
		workspace_->Commands().Register("tools.rendererDiagnostics", {
			[this] { showLegacyDiagnostics_ = !showLegacyDiagnostics_; }, {}});
		workspace_->Menu().RegisterTool({"tools.rendererDiagnostics", "tools.rendererDiagnostics"});
		workspace_->Viewport().SetCameraPreviewTexture(ImGuiRenderer::CameraPreviewTexture);

		return {};
	}

	Result<void> EditorApplication::Update(const float deltaTime) {
		if(session_.IsPlaying() && !editPose_) editPose_ = modelPose_;
		if(!session_.IsPlaying() && editPose_) { modelPose_ = std::move(*editPose_); editPose_.reset(); }
		auto* preview = session_.GetPreviewLevel();
		if(preview == nullptr) return {};
		// Transform解決より前にSceneの動きを適用する。同じframeを複数回描画しても時刻は進まない。
		if(!std::isfinite(deltaTime) || deltaTime < 0.0f) return session_.UpdatePreview(deltaTime);
		const float step = session_.IsPlaying() && !session_.IsPaused() ? deltaTime : 0.0f;
		modelPose_.Update(step);
		return session_.UpdatePreview(deltaTime);
	}

	Result<void> EditorApplication::PrepareRender(const uint32_t width, const uint32_t height) {
		if(sceneGeneration_ != session_.Generation()) {
			// 別LevelのEntity番号が一致しても、旧メッシュを仮表示しない。
			renderScene_.primitives.clear();
			sceneGeneration_ = session_.Generation();
			// Probe history and a saved bake belong to one document's geometry/light setup.
			++renderScene_.ddgi.revision;
			const LevelLightingSettings lighting = session_.GetLevel() ? session_.GetLevel()->Lighting().value_or(LevelLightingSettings{}) : LevelLightingSettings{};
			renderScene_.ambientIrradiance=lighting.skyIrradiance;
			const auto environmentRevision=renderScene_.environment.revision; // Reuse identical baked IBL on Level switches.
			renderScene_.environment=lighting.environment;
			renderScene_.environment.revision=environmentRevision;
			const auto environmentPath=AssetCatalog::Utf8(lighting.environment.imagePath);
			std::snprintf(environmentPath_.data(),environmentPath_.size(),"%s",environmentPath.c_str());
			renderScene_.ddgi.intensity=lighting.probes.intensity;
			renderScene_.ddgi.origin=lighting.probes.origin;
			renderScene_.ddgi.spacing=lighting.probes.spacing;
			renderScene_.ddgi.counts=lighting.probes.counts;
			renderScene_.ddgi.raysPerProbe=lighting.probes.raysPerProbe;
			++renderScene_.bakedLighting.revision;
			renderScene_.bakedLighting.enabled = false;
			if(const auto* document = session_.GetLevelDocument()) {
				auto stem = document->GetFilePath().value_or(std::filesystem::path{"UnsavedLevel"}).stem();
				stem += ".nxgi";
				renderScene_.bakedLighting.cachePath = std::filesystem::path{"Generated/Lighting"} / stem;
				auto path = AssetCatalog::Utf8(renderScene_.bakedLighting.cachePath);
				std::snprintf(bakedPath_.data(),bakedPath_.size(),"%s",path.c_str());
			}
		}
		if(workspace_) {
			const auto& mode = workspace_->EditorPreferences().giMode;
			renderScene_.ddgi.enabled = mode == "DDGI";
			renderScene_.globalIllumination.enabled = mode == "RTGI";
			renderScene_.bakedLighting.enabled = mode == "Baked";
		}
		uint32_t sceneWidth = width, sceneHeight = height;
		if(graphicsSystem_) {
			// Panelの前frame要求を記録開始前に適用し、CameraとRTに同じaspectを使う。
			auto result = graphicsSystem_->PrepareSceneViewport();
			if(!result) return result;
			const auto image = graphicsSystem_->GetSceneViewportImage();
			if(image.width && image.height) { sceneWidth = image.width; sceneHeight = image.height; }
		}
		auto* preview = session_.GetPreviewLevel();
		if(preview == nullptr || preview->GetScene() == nullptr || sceneHeight == 0) return {};
		const float aspect = static_cast<float>(sceneWidth) / static_cast<float>(sceneHeight);
		if(graphicsSystem_) graphicsSystem_->SetCameraPreviewActive(false);
		const auto previousPrimitives = renderScene_.primitives;
		const auto previousAssetRevision = uploadedAssetRevision_;
		auto extracted = extractor_.ExtractInto(*preview->GetScene(), aspect, renderScene_, session_.IsPlaying());
		if(extracted && modelLibrary_ && graphicsSystem_) {
            std::vector<ModelAssetLibrary::Request> requests;
            requests.reserve(renderScene_.primitives.size());
            for(const auto& item : renderScene_.primitives) requests.push_back({item.shape, item.meshAsset, item.baseColorTexture});
            auto preparation = PrepareAssets(std::move(requests));
            if(!preparation) return preparation;
            for(auto& item : renderScene_.primitives) {
                auto range = modelLibrary_->FindRange(item.shape, item.meshAsset, item.baseColorTexture);
                if(range) {
                    item.nodeBegin = range->begin; item.nodeEnd = range->end;
                    item.materialBegin = range->materialBegin;
                    if(item.shape != PrimitiveShape::Asset) item.nodeIndex = PrimitiveRenderItem::kAllNodes;
                    if(item.nodeIndex != PrimitiveRenderItem::kAllNodes) {
                        if(item.nodeIndex >= range->end - range->begin) item.nodeEnd = 0;
                        else item.nodeIndex += range->begin;
                    }
                } else {
                    // 準備中は同じEntityの旧メッシュを表示し、Transformや選択の編集は継続する。
                    const auto previous = std::ranges::find(previousPrimitives, item.sourceObject, &PrimitiveRenderItem::sourceObject);
                    if(previous != previousPrimitives.end() && uploadedAssetRevision_ == previousAssetRevision) {
                        item.nodeBegin = previous->nodeBegin; item.nodeEnd = previous->nodeEnd;
                        item.nodeIndex = previous->nodeIndex; item.materialBegin = previous->materialBegin;
                    } else item.nodeEnd = 0;
                }
                if(!session_.IsPlaying()) for(auto id : session_.Selection().All()) {
                    const auto entity = preview->FindEntity(id);
                    const auto local = entity.GetId();
                    item.selected |= entity.IsAlive() && item.sourceObject.value == ((static_cast<uint64_t>(local.generation) << 32) | local.index);
                }
            }
        }
		if(!extracted) {
			session_.Diagnostics().Set("scene", extracted.error().GetMessageText(), true);
			// 描画抽出に失敗したフレームは古い映像を描かず、診断を表示して編集を継続する。
			renderScene_.primitives.clear(); renderScene_.camera.reset();
		} else session_.Diagnostics().Resolve("scene");
		if(workspace_) workspace_->ViewportEditor().Prepare(renderScene_, aspect);
		const bool previewRequested = extracted && graphicsSystem_ && workspace_ && session_.GetCameraPreview().IsValid() && workspace_->Viewport().IsCameraPreviewVisible();
		if(graphicsSystem_) { auto prepared = PrepareCameraPreview(previewRequested); if(!prepared) return prepared; }
		if(extracted && graphicsSystem_ && workspace_ && session_.GetCameraPreview().IsValid()) {
			auto camera = session_.ResolveCameraPreview(16.0f / 9.0f);
			if(!camera) session_.Diagnostics().Set("preview.camera", camera.error().GetMessageText(), true);
			else {
				session_.Diagnostics().Resolve("preview.camera");
				if(workspace_->Viewport().IsCameraPreviewVisible()) {
					cameraPreviewScene_ = renderScene_;
					cameraPreviewScene_.camera = *camera;
					for(auto& item : cameraPreviewScene_.primitives) item.selected = false;
					if(previewAssetRevision_ != uploadedAssetRevision_) previewInitializationFailed_ = false;
					if(cameraPreviewRenderer_ && !previewInitializationFailed_) {
						// 設定値は主ビューと揃え、GPU資源と履歴はプレビュー側で独立して保持する。
						if(shadows_) static_cast<void>(cameraPreviewRenderer_->GetShadows().SetSettings(shadows_->GetSettings()));
						if(optics_) static_cast<void>(cameraPreviewRenderer_->GetOptics().SetSettings(optics_->GetSettings()));
						if(materialRenderer_) {
							const auto materials = materialRenderer_->GetOpticalMaterials();
							for(size_t i=0; i<materials.size(); ++i)
								static_cast<void>(cameraPreviewRenderer_->SetOpticalMaterial(static_cast<uint32_t>(i), materials[i].optics));
						}
						graphicsSystem_->SetCameraPreviewActive(true);
					}
				}
			}
		} else {
			session_.Diagnostics().Resolve("preview.camera");
			if(!session_.GetCameraPreview().IsValid()) {
				previewInitializationFailed_ = false; session_.Diagnostics().Resolve("preview.renderer");
			}
		}
		// 編集用カメラでは描画を継続しつつ、ゲーム用カメラの競合を知らせる。
		int primaryCameras = 0;
		preview->GetScene()->ForEach<CameraComponent>([&](Entity, CameraComponent& camera) {
			if(camera.enabled && camera.primary) ++primaryCameras;
		});
		if(primaryCameras > 1) session_.Diagnostics().Set("camera.primary", "Multiple enabled primary cameras. Choose one game camera.");
		else session_.Diagnostics().Resolve("camera.primary");
		return {};
	}

	void EditorApplication::BeginFrame() {
		// フォントサイズと共通StyleはImGuiがframeの寸法を計算する前に反映する。
		if(workspace_) workspace_->PrepareFrame();
		guiContext_.SetTheme(UI::CurrentEditorTheme());
		imguiRenderer_.BeginFrame();
		guiContext_.BeginFrame();
		ShowGui();
		if(showDemoWindow_) ImGui::ShowDemoWindow(&showDemoWindow_);
	}

	void EditorApplication::Record(GraphicsContext& context) {
		guiContext_.EndFrame();
		imguiRenderer_.Record(context);
	}

	void EditorApplication::Shutdown() noexcept {
		FinishAssetPreparation();
		ReinstancingService::Get().Unregister(this);
		editPose_.reset();
		if(workspace_) {
			try { workspace_->Shutdown(); }
			catch(...) { NEXUS_LOG_WARNING("Editor", "Failed to persist workspace during shutdown."); }
			workspace_.reset();
		}
		graphicsSystem_ = nullptr;
		guiContext_.Shutdown();
		imguiRenderer_.Shutdown();
		modelPose_.Shutdown();
		renderScene_ = {};
		session_.Close();
	}

	bool EditorApplication::HandleWindowMessage(
		void* const		window,
		const uint32_t	message,
		const uintptr_t wParam,
		const intptr_t	lParam) noexcept {
		if(message == WM_DPICHANGED && workspace_) workspace_->SetDpiScale(static_cast<float>(HIWORD(wParam)) / 96.0f);
		// Platformの既存購読口でWM_CLOSEを保留し、Menuと同じ保存確認を通す。
		if(message == WM_CLOSE && workspace_) {
			try { workspace_->RequestExit(); }
			catch(...) { NEXUS_LOG_ERROR("Editor", "Failed to prepare editor close request."); }
			return true;
		}
		return imguiRenderer_.HandleWindowMessage(window, message, wParam, lParam);
	}

} // namespace NexusEngine
