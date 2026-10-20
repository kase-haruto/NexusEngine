
// engine
#include "Core/Framework/NexusFramework.h"
#include "Editor/Core/EditorApplication.h"
#include "Graphics/Renderer/PrimitiveRenderer.h"
#include "Foundation/Logging/Logger.h"
#include "Foundation/Utility/String/ConvertString.h"

/*-----------------------------------------------------------------------------------------
 * main
 * - エンジンのエントリーポイント
 *---------------------------------------------------------------------------------------*/
int main() {
	NexusEngine::EditorApplication editor;
	NexusEngine::PrimitiveRenderer primitiveRenderer;
	editor.SetShadowSystem(&primitiveRenderer.GetShadows());
	editor.SetOpticalSystem(&primitiveRenderer.GetOptics());
	editor.SetMaterialRenderer(&primitiveRenderer);


	NexusEngine::FrameworkDesc desc;
	NexusEngine::EditorSettingsStore settings;
	settings.Load(std::filesystem::current_path());
	desc.graphics.assetDirectory = std::filesystem::path{NexusEngine::ConvertString(settings.project.assetPath)};
	// CPU Sceneを先に構築し、Renderer初期化へ選択済みAssetと姿勢を接続する。
	// GPU Resourceの所有はRenderer/Graphics側に残すため、SceneにはDeviceを渡さない。
	auto sceneResult = editor.CreateScene(desc.graphics.assetDirectory);
	if(!sceneResult) {
		NEXUS_LOG_ERROR("Scene", sceneResult.error().GetMessageText());
		return 1;
	}
	primitiveRenderer.SetRenderScene(&editor.GetRenderScene());
	primitiveRenderer.SetModelInput(&editor.GetModelAsset(), &editor.GetModelPose());
	desc.mode = NexusEngine::FrameworkMode::Editor;
	desc.renderer = &primitiveRenderer;
	desc.renderExtension = &editor;
	desc.updateClient = &editor;
	desc.messageHandler = [](void* userData, HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
		return static_cast<NexusEngine::EditorApplication*>(userData)->HandleWindowMessage(
			window, message, static_cast<uintptr_t>(wParam), static_cast<intptr_t>(lParam));
	};
	desc.messageHandlerUserData = &editor;
	NexusEngine::NexusFramework framework;
	return framework.Run(desc);
}
