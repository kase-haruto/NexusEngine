
// engine
#include "Core/Framework/NexusFramework.h"
#include "Editor/ImGui/ImGuiRenderer.h"

/*-----------------------------------------------------------------------------------------
 * main
 * - エンジンのエントリーポイント
 *---------------------------------------------------------------------------------------*/
int main() {
	NexusEngine::ImGuiRenderer imguiRenderer;
	NexusEngine::FrameworkDesc desc;
	desc.renderExtension = &imguiRenderer;
	desc.messageHandler = [](void* userData, HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
		return static_cast<NexusEngine::ImGuiRenderer*>(userData)->HandleWindowMessage(
			window, message, static_cast<uintptr_t>(wParam), static_cast<intptr_t>(lParam));
	};
	desc.messageHandlerUserData = &imguiRenderer;
	NexusEngine::NexusFramework framework;
	return framework.Run(desc);
}
