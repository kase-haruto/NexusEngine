#include "EditorApplication.h"
#include "ScenePanels.h"
#include "Graphics/Model/ModelLoader.h"

#include "Graphics/Shadow/ShadowSystem.h"
#include "Graphics/RayTracing/OpticalSystem.h"
#include "Graphics/Renderer/PrimitiveRenderer.h"
#include "Samples/OpticalDemoScene.h"
#include "Foundation/Logging/Logger.h"
#include <cmath>

#include "Runtime/Scene/Components/CameraComponent.h"
#include "Runtime/Scene/Components/PrimitiveRenderComponent.h"
#include "Runtime/Scene/Components/TransformComponent.h"
#include "ThirdParty/imgui/imgui.h"

namespace NexusEngine {
	Result<void> EditorApplication::CreateScene(const std::filesystem::path& assetDirectory) {
		// Sceneが使用するモデルとClipを選択する。GPU初期化前にもCPUだけで構築できる。
		modelPose_.Shutdown();
		cameraEntity_	= {};
		rotatingEntity_ = {};
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
		auto poseResult = modelPose_.Initialize(modelAsset_);
		if(!poseResult) return poseResult;

		if(!modelAsset_.animations.empty()) {
			poseResult = modelPose_.Play(0, true);
			if(!poseResult) {
				return poseResult;
			}
		}
		rotationRadians_	   = {};
		cameraRotationRadians_ = cameraInitRotation_;
		auto levelResult	   = session_.Create();

		if(!levelResult) {
			return levelResult;
		}

		// 既存描画デモをLevelデータへ移し、Runtime→Extraction→Renderer経路を実際に通す。
		cameraEntity_ = session_.GetLevel()->CreateEntity("Camera");
		if(auto* cameraSettings = cameraEntity_.AddComponent<CameraComponent>()) {
			cameraSettings->verticalFieldOfViewRadians = 1.0471975512f;
		}
		if(cameraEntity_.GetComponent<TransformComponent>()) {
			TransformComponent* cameraTransform = cameraEntity_.GetComponent<TransformComponent>();
			cameraTransform->translation		= cameraInitTranslation_;
			cameraTransform->rotation			= Quaternion::FromEulerRadians(cameraInitRotation_);
		}

		// 各Entityが1 Nodeだけを描画する。TransformはModel内の配置に対する追加変換。
		for(size_t nodeIndex = 0; nodeIndex < modelAsset_.nodes.size(); ++nodeIndex) {
			const auto& node = modelAsset_.nodes[nodeIndex];
			if(node.meshIndices.empty()) continue;
			auto entity = session_.GetLevel()->CreateEntity(node.name.empty() ? "Model Node " + std::to_string(nodeIndex) : node.name);
			if(auto* primitive = entity.AddComponent<PrimitiveRenderComponent>()) {
				primitive->nodeIndex = static_cast<uint32_t>(nodeIndex);
			}
		}
		if(auto* id = cameraEntity_.GetComponent<PersistentIdComponent>()) session_.Select(id->value);
		return session_.GetLevel()->Update(0.0f);
	}

	void EditorApplication::ShowGui() {
		// フレームが開始していない場合は終了
		if(!guiContext_.IsFrameActive()) {
			return;
		}
		DrawLevelDemoGui(session_);
		DrawHierarchy(session_);
		DrawTransformInspector(session_);
		if(optics_ && ImGui::Begin("Ray Traced Optics")) {
			ImGui::TextUnformatted("Left sphere: mirror / Right sphere: refractive glass");
			ImGui::TextUnformatted("Sponza architecture reflects / refracts through the spheres.");
			auto settings = optics_->GetSettings();
			bool changed = ImGui::Checkbox("Reflection / Transmission", &settings.enabled);
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

		outlinerPanel_->ShowGui();

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
				ImGui::DragFloat3("Light Direction", renderScene_.directionalLight.direction.data(), 0.01f);
				ImGui::DragFloat("Angular Radius (rad)", &renderScene_.directionalLight.angularRadius, 0.001f, 0, 1.5f);
				ImGui::DragFloat("Light Intensity", &renderScene_.directionalLight.intensity, 0.01f, 0, 10);
			}
			ImGui::End();
		}
	}

	Result<void> EditorApplication::Initialize(const GraphicsRenderExtensionContext& context) {
		auto rendererResult = imguiRenderer_.Initialize(context);
		if(!rendererResult) {
			cameraEntity_	= {};
			rotatingEntity_ = {};
			session_.Close();
			return rendererResult;
		}

		auto guiResult = guiContext_.Initialize();
		if(!guiResult) {
			imguiRenderer_.Shutdown();
			cameraEntity_	= {};
			rotatingEntity_ = {};
			session_.Close();
			return guiResult;
		}

		outlinerPanel_ = std::make_unique<OutlinerPanel>();

		return {};
	}

	Result<void> EditorApplication::Update(const float deltaTime) {

		if(session_.GetLevel() == nullptr) return {};
		// Transform解決より前にSceneの動きを適用する。同じframeを複数回描画しても時刻は進まない。
		if(session_.GetLevel()->GetScene() == nullptr) return session_.GetLevel()->Update(deltaTime);
		if(!std::isfinite(deltaTime) || deltaTime < 0.0f) return session_.GetLevel()->Update(deltaTime);
		modelPose_.Update(deltaTime);
		return session_.GetLevel()->Update(deltaTime);
	}

	Result<void> EditorApplication::PrepareRender(const uint32_t width, const uint32_t height) {
		if(session_.GetLevel() == nullptr || session_.GetLevel()->GetScene() == nullptr || height == 0) return {};
		return extractor_.ExtractInto(*session_.GetLevel()->GetScene(), static_cast<float>(width) / static_cast<float>(height), renderScene_);
	}

	void EditorApplication::BeginFrame() {
		imguiRenderer_.BeginFrame();
		guiContext_.BeginFrame();
		DrawDockSpace();
		ShowGui();
		if(showDemoWindow_) ImGui::ShowDemoWindow(&showDemoWindow_);
	}

	void EditorApplication::Record(GraphicsContext& context) {
		guiContext_.EndFrame();
		imguiRenderer_.Record(context);
	}

	void EditorApplication::Shutdown() noexcept {
		guiContext_.Shutdown();
		imguiRenderer_.Shutdown();
		cameraEntity_	= {};
		rotatingEntity_ = {};
		modelPose_.Shutdown();
		renderScene_ = {};
		session_.Close();
	}

	bool EditorApplication::HandleWindowMessage(
		void* const		window,
		const uint32_t	message,
		const uintptr_t wParam,
		const intptr_t	lParam) noexcept {
		return imguiRenderer_.HandleWindowMessage(window, message, wParam, lParam);
	}

	void EditorApplication::DrawDockSpace() {
		ImGui::DockSpaceOverViewport(
			0,
			ImGui::GetMainViewport(),
			ImGuiDockNodeFlags_PassthruCentralNode);
		// DrawMainMenuBar();
	}

	void EditorApplication::DrawMainMenuBar() {
		if(!ImGui::BeginMainMenuBar()) return;

		if(ImGui::BeginMenu("Window")) {
			ImGui::MenuItem("Dear ImGui Demo", nullptr, &showDemoWindow_);
			ImGui::EndMenu();
		}

		ImGui::EndMainMenuBar();
	}
} // namespace NexusEngine
