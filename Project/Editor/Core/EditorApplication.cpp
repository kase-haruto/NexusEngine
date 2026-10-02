#include "EditorApplication.h"

#include "ThirdParty/imgui/imgui.h"

namespace NexusEngine {
	Result<void> EditorApplication::Initialize(const GraphicsRenderExtensionContext& context) {
		auto rendererResult = imguiRenderer_.Initialize(context);
		if(!rendererResult) return rendererResult;

		auto guiResult = guiContext_.Initialize();
		if(!guiResult) {
			imguiRenderer_.Shutdown();
			return guiResult;
		}
		return {};
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
