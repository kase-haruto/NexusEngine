#include "EditorApplication.h"

#include <cmath>
#include "Graphics/Model/ModelLoader.h"

#include "ThirdParty/imgui/imgui.h"
#include "Runtime/Scene/Components/CameraComponent.h"
#include "Runtime/Scene/Components/PrimitiveRenderComponent.h"

namespace NexusEngine {
	Result<void> EditorApplication::CreateScene(const std::filesystem::path& assetDirectory) {
		// Sceneが使用するモデルとClipを選択する。GPU初期化前にもCPUだけで構築できる。
		modelPose_.Shutdown();
		rotatingEntity_ = {};
		level_.Unload();
		auto asset = ModelLoader{}.LoadGltf(assetDirectory / L"Models/SimpleSkin/SimpleSkin.gltf");
		if(!asset) return std::unexpected(std::move(asset.error()));
		modelAsset_ = std::move(*asset);
		auto poseResult = modelPose_.Initialize(modelAsset_);
		if(!poseResult) return poseResult;
		if(!modelAsset_.animations.empty()) {
			poseResult = modelPose_.Play(0, true);
			if(!poseResult) return poseResult;
		}
		rotationRadians_ = {};
		auto levelResult = level_.LoadEmpty();
		if(!levelResult) return levelResult;
		// 既存描画デモをLevelデータへ移し、Runtime→Extraction→Renderer経路を実際に通す。
		Entity camera = level_.CreateEntity("Camera");
		CameraComponent cameraSettings;
		cameraSettings.verticalFieldOfViewRadians = 1.0471975512f;
		static_cast<void>(camera.AddComponent<CameraComponent>(cameraSettings));
		Entity primitive = level_.CreateEntity("Primitive");
		static_cast<void>(primitive.AddComponent<PrimitiveRenderComponent>());
		auto* transform = primitive.GetComponent<TransformComponent>();
		transform->translation = { 0.0f, -0.75f, 3.0f };
		transform->scale = { 0.75f, 0.75f, 0.75f };


		rotatingEntity_ = primitive;
		return level_.Update(0.0f);
	}

	Result<void> EditorApplication::Initialize(const GraphicsRenderExtensionContext& context) {
		auto rendererResult = imguiRenderer_.Initialize(context);
		if(!rendererResult) {
			rotatingEntity_ = {};
			level_.Unload();
			return rendererResult;
		}

		auto guiResult = guiContext_.Initialize();
		if(!guiResult) {
			imguiRenderer_.Shutdown();
			rotatingEntity_ = {};
			level_.Unload();
			return guiResult;
		}
		return {};
	}

	Result<void> EditorApplication::Update(const float deltaTime) {

		// Transform解決より前にSceneの動きを適用する。同じframeを複数回描画しても時刻は進まない。
		if(level_.GetScene() == nullptr) return level_.Update(deltaTime);
		if(!std::isfinite(deltaTime) || deltaTime < 0.0f) return level_.Update(deltaTime);
		modelPose_.Update(deltaTime);
		if(rotatingEntity_.IsAlive()) {
			if(auto* transform = rotatingEntity_.GetComponent<TransformComponent>()) {
				// 長時間稼働で角度が増え続け、floatの精度が落ちることを防ぐ。
				constexpr float turn = 6.2831853072f;
				rotationRadians_.x = std::fmod(rotationRadians_.x + rotationSpeed_.x * deltaTime, turn);
				rotationRadians_.y = std::fmod(rotationRadians_.y + rotationSpeed_.y * deltaTime, turn);
				rotationRadians_.z = std::fmod(rotationRadians_.z + rotationSpeed_.z * deltaTime, turn);
				transform->rotation = Quaternion::FromEulerRadians(rotationRadians_);
			}
		}
		return level_.Update(deltaTime);
	}

	Result<void> EditorApplication::PrepareRender(const uint32_t width, const uint32_t height) {
		if(level_.GetScene() == nullptr || height == 0) return {};
		return extractor_.ExtractInto(*level_.GetScene(), static_cast<float>(width) / static_cast<float>(height), renderScene_);
	}

	void EditorApplication::BeginFrame() {
		imguiRenderer_.BeginFrame();
		guiContext_.BeginFrame();
		DrawDockSpace();
		if(showDemoWindow_) ImGui::ShowDemoWindow(&showDemoWindow_);
	}

	void EditorApplication::Record(GraphicsContext& context) {
		guiContext_.EndFrame();
		imguiRenderer_.Record(context);
	}

	void EditorApplication::Shutdown() noexcept {
		guiContext_.Shutdown();
		imguiRenderer_.Shutdown();
		rotatingEntity_ = {};
		modelPose_.Shutdown();
		renderScene_ = {};
		level_.Unload();
	}

	bool EditorApplication::HandleWindowMessage(
		void* const window,
		const uint32_t message,
		const uintptr_t wParam,
		const intptr_t lParam) noexcept {
		return imguiRenderer_.HandleWindowMessage(window, message, wParam, lParam);
	}

	void EditorApplication::DrawDockSpace() {
		ImGui::DockSpaceOverViewport(
			0,
			ImGui::GetMainViewport(),
			ImGuiDockNodeFlags_PassthruCentralNode);
		DrawMainMenuBar();
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
