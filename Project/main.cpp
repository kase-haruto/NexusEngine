
// engine
#include "Core/Framework/NexusFramework.h"
#include "Editor/Core/EditorApplication.h"
#include "Editor/Core/EditorProjectPaths.h"
#include "Graphics/Renderer/PrimitiveRenderer.h"
#include "Foundation/Logging/Logger.h"
#include "Foundation/Utility/String/ConvertString.h"
#include "Runtime/Application/GameApplication.h"
#include "Game/SampleGame.h"
#include "Samples/StartupAssets.h"
#include <iostream>
#include <string_view>

/*-----------------------------------------------------------------------------------------
 * main
 * - エンジンのエントリーポイント
 *---------------------------------------------------------------------------------------*/
int wmain(int argc, wchar_t** argv) {
    // Scene loading runs before Framework::Run, so its errors need logging too.
    NexusEngine::Logger::Get().InitializeDefaultSinks();
    const auto failStartup = [](const std::string& message) {
        NEXUS_LOG_ERROR("Startup", message);
        std::cerr << message << '\n';
        return 1;
    };
    bool gameMode = false, checkGame = false;
    std::filesystem::path projectArgument, levelArgument;
    for(int index = 1; index < argc; ++index) {
        const std::wstring_view argument{argv[index]};
        if(argument == L"--game") gameMode = true;
        else if(argument == L"--check-game") checkGame = true;
        else if(argument == L"--project" || argument == L"--level") {
            if(index + 1 == argc) return failStartup("Missing value for --project or --level.");
            (argument == L"--project" ? projectArgument : levelArgument) = argv[++index];
        } else if(argument == L"--help") {
            std::cout << "NexusEngine [--game] [--project <root>] [--level <file>] [--check-game]\n"
                         "Default: Editor. --game: standalone game window.\n"
                         "--check-game: with --game, validate CPU startup/update/render extraction and exit.\n";
            return 0;
        } else return failStartup("Unknown command line argument.");
    }
    if(checkGame && !gameMode) return failStartup("--check-game requires --game.");
    std::error_code pathError;
    auto projectRoot = projectArgument.empty()
        ? NexusEngine::FindProjectRoot(std::filesystem::current_path(pathError))
        : std::optional<std::filesystem::path>{std::filesystem::absolute(projectArgument, pathError)};
    if(pathError) return failStartup("Cannot resolve project path: " + pathError.message());
    if(!projectRoot && projectArgument.empty()) {
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
    const auto project = NexusEngine::LoadProjectSettings(*projectRoot);
    if(!project) return failStartup(project.error().GetMessageText());
    const auto assetRoot = std::filesystem::path{NexusEngine::ConvertString(project->assetPath)};
    const auto levelPath = levelArgument.empty()
        ? std::filesystem::path{NexusEngine::ConvertString(project->startupLevel)} : levelArgument;

    if(gameMode) {
        NexusEngine::GameApplication game;
        if(!game.SetGameLevelSetup(NexusGame::SetupSampleGame)) return failStartup("Cannot configure game systems.");
        auto result = game.Load(levelPath, assetRoot, [assetRoot] { return NexusEngine::LoadSampleStartupAsset(assetRoot); });
        if(!result) return failStartup(result.error().GetMessageText());
        if(checkGame) {
            result = game.Update(1.0f / 60.0f);
            if(result) result = game.PrepareRender(1280, 720);
            if(!result) return failStartup(result.error().GetMessageText());
            std::cout << "PASS: game startup, systems, assets and render extraction.\n";
            return 0;
        }
        NexusEngine::PrimitiveRenderer renderer;
        renderer.SetRenderScene(&game.GetRenderScene());
        renderer.SetModelInput(&game.GetModelAsset(), &game.GetModelPose());
        NexusEngine::FrameworkDesc gameDesc;
        gameDesc.mode = NexusEngine::FrameworkMode::Game;
        gameDesc.graphics.assetDirectory = assetRoot;
        gameDesc.renderer = &renderer;
        gameDesc.updateClient = &game;
        NexusEngine::NexusFramework framework;
        return framework.Run(gameDesc);
    }

	NexusEngine::EditorApplication editor;
	if(!editor.SetGameLevelSetup(NexusGame::SetupSampleGame)) return failStartup("Cannot configure game systems.");
	NexusEngine::PrimitiveRenderer primitiveRenderer;
	editor.SetShadowSystem(&primitiveRenderer.GetShadows());
	editor.SetOpticalSystem(&primitiveRenderer.GetOptics());
	editor.SetMaterialRenderer(&primitiveRenderer);


	NexusEngine::FrameworkDesc desc;
	desc.graphics.assetDirectory = assetRoot;
	// CPU Sceneを先に構築し、Renderer初期化へ選択済みAssetと姿勢を接続する。
	// GPU Resourceの所有はRenderer/Graphics側に残すため、SceneにはDeviceを渡さない。
	auto sceneResult = editor.CreateScene(desc.graphics.assetDirectory, levelPath);
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
