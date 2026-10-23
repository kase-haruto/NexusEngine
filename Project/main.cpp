
// engine
#include "Core/Framework/NexusFramework.h"
#include "Editor/Core/EditorApplication.h"
#include "Editor/Core/EditorProjectPaths.h"
#include "Graphics/Renderer/PrimitiveRenderer.h"
#include "Foundation/Logging/Logger.h"
#include "Foundation/Utility/String/ConvertString.h"

/*-----------------------------------------------------------------------------------------
 * main
 * - エンジンのエントリーポイント
 *---------------------------------------------------------------------------------------*/
int main() {
    // Scene loading runs before Framework::Run, so its errors need logging too.
    NexusEngine::Logger::Get().InitializeDefaultSinks();
    const auto failStartup = [](const std::string& message) {
        NEXUS_LOG_ERROR("Startup", message);
        const auto wide = NexusEngine::ConvertString(message);
        MessageBoxW(nullptr, wide.c_str(), L"NexusEngine - Startup failed", MB_OK | MB_ICONERROR);
        return 1;
    };
    std::error_code pathError;
    auto projectRoot = NexusEngine::FindEditorProjectRoot(std::filesystem::current_path(pathError));
    if(!projectRoot) {
        std::wstring executable(32768, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if(length > 0 && length < executable.size()) {
            executable.resize(length);
            projectRoot = NexusEngine::FindEditorProjectRoot(std::filesystem::path(executable).parent_path());
        }
    }
    if(!projectRoot) return failStartup("Cannot locate the project folder (Config/Project.settings, Resources/Assets and Shaders).");
    std::filesystem::current_path(*projectRoot, pathError);
    if(pathError) return failStartup("Cannot access the project folder: " + pathError.message());

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
		return failStartup(sceneResult.error().GetMessageText());
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
