#include "EditorApplication.h"

#include "Graphics/Model/ModelLoader.h"
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
		level_.Unload();

		// モデル読み込み
		auto asset = ModelLoader{}.LoadGltf(assetDirectory / L"Models/Sponza/Sponza.gltf");
		if(!asset) {
			// 読み込みに失敗したらエラーを返す
			return std::unexpected(std::move(asset.error()));
		}

		// モデルアセットに所有権を移動
		modelAsset_		= std::move(*asset);
		auto poseResult = modelPose_.Initialize(modelAsset_);
		if(!poseResult) return poseResult;

		if(!modelAsset_.animations.empty()) {
			poseResult = modelPose_.Play(0, true);
			if(!poseResult) {
				return poseResult;
			}
		}
		rotationRadians_	   = {};
		cameraRotationRadians_ = {};
		auto levelResult	   = level_.LoadEmpty();

		if(!levelResult) {
			return levelResult;
		}

		// 既存描画デモをLevelデータへ移し、Runtime→Extraction→Renderer経路を実際に通す。
		cameraEntity_ = level_.CreateEntity("Camera");
		if(auto* cameraSettings = cameraEntity_.AddComponent<CameraComponent>()) {
			cameraSettings->verticalFieldOfViewRadians = 1.0471975512f;
		}
		if(cameraEntity_.GetComponent<TransformComponent>()) {
			TransformComponent* cameraTransform = cameraEntity_.GetComponent<TransformComponent>();
			cameraTransform->translation		= cameraInitTranslation_;
			cameraTransform->rotation			= Quaternion::FromEulerRadians(cameraInitRotation_);
		}

		// モデルを描画するエンティティ
		rotatingEntity_ = level_.CreateEntity("Primitive");
		static_cast<void>(rotatingEntity_.AddComponent<PrimitiveRenderComponent>());
		return level_.Update(0.0f);
	}

	void EditorApplication::ShowGui() {
		// フレームが開始していない場合は終了
		if(!guiContext_.IsFrameActive()) {
			return;
		}

		// demoGuiWindowの描画
		if(ImGui::Begin("DemoLevelDebugWindow")) {
			// カメラのtransform操作
			if(ImGui::TreeNode("Camera")) {
				if(auto* transform = cameraEntity_.GetComponent<TransformComponent>()) {
					ImGui::DragFloat3("Translate", &transform->translation.x, 0.1f);
					if(ImGui::DragFloat3("Rotation (rad)", &cameraRotationRadians_.x, 0.01f)) {
						transform->rotation = Quaternion::FromEulerRadians(cameraRotationRadians_);
					}
				}
				ImGui::TreePop();
			}
		}
		ImGui::End();
	}

	Result<void> EditorApplication::Initialize(const GraphicsRenderExtensionContext& context) {
		auto rendererResult = imguiRenderer_.Initialize(context);
		if(!rendererResult) {
			cameraEntity_	= {};
			rotatingEntity_ = {};
			level_.Unload();
			return rendererResult;
		}

		auto guiResult = guiContext_.Initialize();
		if(!guiResult) {
			imguiRenderer_.Shutdown();
			cameraEntity_	= {};
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
		level_.Unload();
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
