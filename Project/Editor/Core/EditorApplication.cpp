#include "EditorApplication.h"
#include "Graphics/Model/ModelLoader.h"
#include "Graphics/Model/ModelThumbnail.h"

#include "Graphics/Shadow/ShadowSystem.h"
#include "Graphics/RayTracing/OpticalSystem.h"
#include "Graphics/Renderer/PrimitiveRenderer.h"
#include "Graphics/Renderer/GraphicsSystem.h"
#include "Samples/OpticalDemoScene.h"
#include "Foundation/Logging/Logger.h"
#include "Foundation/Utility/String/ConvertString.h"
#include "Graphics/Color/ColorSpace.h"
#include <cmath>
#include <algorithm>
#include <cstdio>

#include "Runtime/Scene/Components/DirectionalLightComponent.h"
#include "ThirdParty/imgui/imgui.h"
#include "Editor/ImGui/ImGuiStyle.h"

namespace NexusEngine {
	Result<void> EditorApplication::CreateScene(const std::filesystem::path& assetDirectory) {
		editPose_.reset();
        reportedAssetErrors_.clear();
		// Sceneが使用するモデルとClipを選択する。GPU初期化前にもCPUだけで構築できる。
		modelPose_.Shutdown();
		session_.Close();

		// モデル読み込み
		auto asset = ModelLoader{}.LoadGltf(assetDirectory / L"Models/Sponza/Sponza.gltf");
		if(!asset) {
			// 読み込みに失敗したらエラーを返す
			return std::unexpected(std::move(asset.error()));
		}

		// モデルアセットに所有権を移動
		modelAsset_		= std::move(*asset);
		auto sphereMaterials = AppendSponzaOpticalSpheres(modelAsset_);
		if(!sphereMaterials) {
			return std::unexpected(std::move(sphereMaterials.error()));
		}
		mirrorMaterialSlot_ = sphereMaterials->mirror;
		glassMaterialSlot_ = sphereMaterials->glass;
		auto testScene = AppendPBRTestScene(modelAsset_);
		if(!testScene) return testScene;
		auto poseResult = modelPose_.Initialize(modelAsset_);
        modelLibrary_ = std::make_unique<ModelAssetLibrary>(assetDirectory, modelAsset_);
        uploadedAssetRevision_ = modelLibrary_->Revision();
		if(!poseResult) return poseResult;

		if(!modelAsset_.animations.empty()) {
			poseResult = modelPose_.Play(0, true);
			if(!poseResult) {
				return poseResult;
			}
		}
		EditorSettingsStore bootSettings;
		bootSettings.Load(std::filesystem::current_path());
		auto levelResult = session_.Open(std::filesystem::path{ConvertString(bootSettings.project.startupLevel)});
		if(!levelResult) return levelResult;
		renderScene_.ambientIrradiance = {1.5f, 1.8f, 2.4f}; // HDR未指定時の遠方環境の照度
		renderScene_.globalIllumination.enabled = false;
		renderScene_.bakedLighting.enabled = false;
		renderScene_.ddgi.enabled = true;
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
				ImGui::Checkbox("DDGI Enabled",&ddgi.enabled);
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
				ImGui::Checkbox("Baked GI Enabled", &baked.enabled);
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
				ImGui::Checkbox("Diffuse RTGI Enabled", &gi.enabled);
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

		workspace_ = std::make_unique<EditorWorkspace>(session_);
		workspace_->Initialize(std::filesystem::current_path(), editorExtensions_);
		workspace_->SetDpiScale(static_cast<float>(GetDpiForWindow(static_cast<HWND>(context.nativeWindow))) / 96.0f);
		workspace_->ViewportEditor().SetModel(modelAsset_, modelPose_);
        workspace_->AssetEditor().Configure(modelLibrary_->Root(),
            [this](const std::filesystem::path& path, bool texture) -> Result<std::string> {
                auto reference = modelLibrary_->Reference(path);
                if(!reference) return std::unexpected(reference.error());
                if(texture) { auto loaded = modelLibrary_->Texture(*reference); if(!loaded) return std::unexpected(loaded.error()); }
                else { auto loaded = modelLibrary_->Model(*reference); if(!loaded) return std::unexpected(loaded.error()); }
                return reference;
            }, [this](const std::string& reference) -> const ModelAssetData* {
                auto model = modelLibrary_->Model(reference); return model ? *model : nullptr;
            }, [this](const std::string& path, bool texture, int node) -> ImTextureRef {
                std::string reference;
                if(!path.empty()) {
                    auto result = modelLibrary_->Reference(std::filesystem::path(reinterpret_cast<const char8_t*>(path.c_str())));
                    if(!result) return {}; reference = *result;
                }
                const auto key = std::string(texture ? "texture:" : "mesh:") + reference + ":" + std::to_string(node);
                auto cached = imguiRenderer_.FindPreview(key);
                if(cached._TexData) return cached;
                if(!imguiRenderer_.CanCreatePreview()) return {};
                if(texture) {
                    auto bytes = modelLibrary_->Texture(reference); if(!bytes) return {};
                    auto image = WicImageDecoder{}.Decode(**bytes); if(!image) return {};
                    return imguiRenderer_.CreatePreview(key, CreateImageThumbnail(*image));
                }
                auto model = modelLibrary_->Model(reference); if(!model) return {};
                auto image = CreateModelThumbnail(**model, node); if(!image) return {};
                return imguiRenderer_.CreatePreview(key, *image);
            });
		if(!workspace_->Viewport().AttachLocalPreview(imguiRenderer_, ImGuiRenderer::SceneViewportTexture,
			[graphics = context.graphicsSystem](uint32_t width, uint32_t height) { return graphics->RequestSceneViewportSize(width, height); })) {
			workspace_->Shutdown(); workspace_.reset(); guiContext_.Shutdown(); imguiRenderer_.Shutdown();
			return std::unexpected(Error(ErrorCategory::Graphics, 103, "Failed to connect local scene viewport."));
		}
		workspace_->Commands().Register("tools.rendererDiagnostics", {
			[this] { showLegacyDiagnostics_ = !showLegacyDiagnostics_; }, {}});
		workspace_->Menu().RegisterTool({"tools.rendererDiagnostics", "tools.rendererDiagnostics"});
		graphicsSystem_ = context.graphicsSystem;

		return {};
	}

	Result<void> EditorApplication::Update(const float deltaTime) {
		if(session_.IsPlaying() && !editPose_) editPose_ = modelPose_;
		if(!session_.IsPlaying() && editPose_) { modelPose_ = std::move(*editPose_); editPose_.reset(); }
		auto* preview = session_.GetPreviewLevel();
		if(preview == nullptr) return {};
		// Transform解決より前にSceneの動きを適用する。同じframeを複数回描画しても時刻は進まない。
		if(!std::isfinite(deltaTime) || deltaTime < 0.0f) return preview->Update(deltaTime);
		const float step = session_.IsPlaying() && !session_.IsPaused() ? deltaTime : 0.0f;
		modelPose_.Update(step);
		return preview->Update(step);
	}

	Result<void> EditorApplication::PrepareRender(const uint32_t width, const uint32_t height) {
		if(sceneGeneration_ != session_.Generation()) {
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
		auto extracted = extractor_.ExtractInto(*preview->GetScene(), aspect, renderScene_);
        if(extracted && modelLibrary_) {
            for(auto& item : renderScene_.primitives) {
                auto range = modelLibrary_->Resolve(item.meshAsset, item.baseColorTexture);
                if(!range) {
                    const auto key = item.meshAsset + "\n" + item.baseColorTexture;
                    if(reportedAssetErrors_.insert(key).second) NEXUS_LOG_ERROR("Assets", range.error().GetMessageText());
                    item.nodeEnd = 0;
                    continue;
                }
                item.nodeBegin = range->begin; item.nodeEnd = range->end;
                item.materialBegin = range->materialBegin;
                if(item.nodeIndex != PrimitiveRenderItem::kAllNodes) {
                    if(item.nodeIndex >= range->end - range->begin) item.nodeEnd = 0;
                    else item.nodeIndex += range->begin;
                }
                if(!session_.IsPlaying()) for(auto id : session_.Selection().All()) {
                    const auto entity = preview->FindEntity(id);
                    const auto local = entity.GetId();
                    item.selected |= entity.IsAlive() && item.sourceObject.value == ((static_cast<uint64_t>(local.generation) << 32) | local.index);
                }
            }
            if(graphicsSystem_ && uploadedAssetRevision_ != modelLibrary_->Revision()) {
                auto previousAsset = std::move(modelAsset_);
                auto previousPose = modelPose_;
                modelAsset_ = modelLibrary_->Snapshot();
                modelPose_.Shutdown();
                auto initialized = modelPose_.Initialize(modelAsset_);
                if(!initialized) { modelAsset_ = std::move(previousAsset); modelPose_ = std::move(previousPose); return initialized; }
                auto renderer = std::make_unique<PrimitiveRenderer>();
                renderer->SetRenderScene(&renderScene_); renderer->SetModelInput(&modelAsset_, &modelPose_);
                // 初期化はCommandList記録前。旧GPU資源はidle後にだけ解放する。
                auto replaced = graphicsSystem_->ReplaceRenderer(renderer.get());
                if(!replaced) { modelAsset_ = std::move(previousAsset); modelPose_ = std::move(previousPose); return replaced; }
                replacementRenderer_ = std::move(renderer);
                materialRenderer_ = replacementRenderer_.get(); shadows_ = &materialRenderer_->GetShadows(); optics_ = &materialRenderer_->GetOptics();
                uploadedAssetRevision_ = modelLibrary_->Revision();
                NEXUS_LOG_INFO("Assets", "Uploaded model and material asset snapshot.");
                if(workspace_) workspace_->ViewportEditor().SetModel(modelAsset_, modelPose_);
            }
        }
		if(extracted && workspace_) workspace_->ViewportEditor().Prepare(renderScene_, aspect);
		return extracted;
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
