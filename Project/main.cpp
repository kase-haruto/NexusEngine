
// engine
#include "Core/Framework/NexusFramework.h"
#include "Editor/Core/EditorApplication.h"
#include "Graphics/Renderer/PrimitiveRenderer.h"

/*-----------------------------------------------------------------------------------------
 * main
 * - エンジンのエントリーポイント
 *---------------------------------------------------------------------------------------*/
int main() {
	NexusEngine::EditorApplication editor;
	NexusEngine::PrimitiveRenderer primitiveRenderer;
	NexusEngine::FrameworkDesc desc;
	desc.mode = NexusEngine::FrameworkMode::Editor;
	desc.renderer = &primitiveRenderer;
	desc.renderExtension = &editor;
	desc.messageHandler = [](void* userData, HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
		return static_cast<NexusEngine::EditorApplication*>(userData)->HandleWindowMessage(
			window, message, static_cast<uintptr_t>(wParam), static_cast<intptr_t>(lParam));
	};
	desc.messageHandlerUserData = &editor;
	NexusEngine::NexusFramework framework;
	return framework.Run(desc);
}
