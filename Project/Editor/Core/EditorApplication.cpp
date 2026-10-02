#include "EditorApplication.h"

#include "ThirdParty/imgui/imgui.h"
#include "Runtime/Scene/Components/CameraComponent.h"
#include "Runtime/Scene/Components/PrimitiveRenderComponent.h"

namespace NexusEngine {
	Result<void> EditorApplication::Initialize(const GraphicsRenderExtensionContext& context) {
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

		auto rendererResult = imguiRenderer_.Initialize(context);
		if(!rendererResult) {
			level_.Unload();
			return rendererResult;
		}

		auto guiResult = guiContext_.Initialize();
		if(!guiResult) {
			imguiRenderer_.Shutdown();
			level_.Unload();
			return guiResult;
		}
		return {};
	}

	Result<void> EditorApplication::Update(const float deltaTime) {
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
