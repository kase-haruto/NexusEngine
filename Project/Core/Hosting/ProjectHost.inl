#pragma once
// Gameホストが登録方針を明示し、省略時はEngine組み込み型だけを使用する。
#ifndef NEXUS_PROJECT_REFLECTION
#define NEXUS_PROJECT_REFLECTION NexusEngine::ReflectionRegistration{}
#endif
#ifndef NEXUS_PROJECT_SETUP
#define NEXUS_PROJECT_SETUP NexusEngine::GameLevelSetup{}
#endif
#ifndef NEXUS_PROJECT_STARTUP_LOADER
#define NEXUS_PROJECT_STARTUP_LOADER(assetRoot) NexusEngine::ModelAssetLibrary::ReloadStartup{}
#endif

// engine
#include "Core/Framework/NexusFramework.h"
#include "Core/Hosting/BoundedFrameClient.h"
#ifndef NEXUS_GAME_HOST
#include "Editor/Core/EditorApplication.h"
#include "Editor/Services/StandaloneSession.h"
#include "Editor/Services/StandaloneCommands.h"
#include "Platform/Window/StartupSplash.h"
#endif
#include "Runtime/Application/ProjectSettings.h"
#include "Runtime/Project/ProjectService.h"
#ifndef NEXUS_GAME_HOST
#include "Tools/Project/ProjectRegistry.h"
#include "Tools/Project/ProjectBuild.h"
#include "Tools/Project/SourceCodeEditor.h"
#endif
#include "Graphics/Renderer/PrimitiveRenderer.h"
#include "Graphics/PostProcess/PostProcessGraphAsset.h"
#include "Foundation/Logging/Logger.h"
#include "Foundation/Utility/String/ConvertString.h"
#include "Runtime/Application/GameApplication.h"


#include <iostream>
#include <string_view>

/*-----------------------------------------------------------------------------------------
 * main
 * - エンジンのエントリーポイント
 *---------------------------------------------------------------------------------------*/
int wmain(int argc, wchar_t** argv) {
    // Framework開始前のScene読み込み失敗も記録する。
    NexusEngine::Logger::Get().InitializeDefaultSinks();
    const auto failStartup = [](const std::string& message) {
        NEXUS_LOG_ERROR("Startup", message);
        std::cerr << message << '\n';
        return 1;
    };
#ifdef NEXUS_GAME_HOST
    bool gameMode = true;
#else
    bool gameMode = false;
#endif
    bool checkGame = false;
    bool hidden = false;
    uint32_t frameLimit = 0;
    std::filesystem::path projectArgument, levelArgument, engineArgument;
#ifndef NEXUS_GAME_HOST
    std::filesystem::path engineManifestArgument;
#endif
    for(int index = 1; index < argc; ++index) {
        const std::wstring_view argument{argv[index]};
        if(argument == L"--game") gameMode = true;
        else if(argument == L"--check-game") checkGame = true;
        else if(argument == L"--hidden") hidden = true;
        else if(argument == L"--frames") {
            if(index + 1 == argc) return failStartup("Missing frame limit.");
            const std::wstring_view value{argv[++index]}; uint32_t parsed = 0;
            if(value.empty()) return failStartup("Empty frame limit.");
            for(wchar_t character : value) {
                if(character < L'0' || character > L'9' || parsed > 10000) return failStartup("Frame limit must be between 1 and 10000.");
                parsed = parsed * 10 + static_cast<uint32_t>(character - L'0');
            }
            if(!parsed || parsed > 10000) return failStartup("Frame limit must be between 1 and 10000.");
            frameLimit = parsed;
        }
#ifndef NEXUS_GAME_HOST
        else if(argument == L"--engine-manifest") {
            if(index + 1 == argc || std::wstring_view(argv[index + 1]).empty()) return failStartup("Missing Engine manifest.");
            engineManifestArgument = argv[++index];
        }
#endif
        else if(argument == L"--project" || argument == L"--level" || argument == L"--engine") {
            if(index + 1 == argc) return failStartup("Missing value for --project, --engine or --level.");
            if(std::wstring_view(argv[index + 1]).empty()) return failStartup("Empty startup argument.");
            (argument == L"--project" ? projectArgument : argument == L"--engine" ? engineArgument : levelArgument) = argv[++index];
        } else if(argument == L"--help") {
            std::cout << "NexusEngine [--game] [--project <file.nxproject|legacy-root>] [--engine <root>] [--level <file>] [--check-game]\n"
                         "Editor host defaults to Editor; Runtime host defaults to game. --game: standalone game window.\n"
                         "--check-game: with --game, validate CPU startup/update/render extraction and exit.\n"
                         "--frames <1..10000> --hidden: run bounded GPU frames without showing a window.\n";
            return 0;
        } else return failStartup("Unknown command line argument.");
    }
    if(checkGame && !gameMode) return failStartup("--check-game requires --game.");
    if(checkGame && frameLimit) return failStartup("--check-game and --frames are separate CPU/GPU checks.");
#ifndef NEXUS_GAME_HOST
    NexusEngine::StartupSplash startup;
    if(!gameMode && !hidden) {
        auto shown = startup.Show({L"NexusEngine",L"Editor",NexusEngine::ConvertString(std::string(NEXUS_HOST_ENGINE_VERSION))});
        if(!shown) NEXUS_LOG_WARNING("Startup",shown.error().GetMessageText());
    }
    startup.SetStatus(L"Loading project settings...");
#endif
    const auto run = [&](NexusEngine::FrameworkDesc& description, NexusEngine::IFrameworkUpdateClient& client) {
        NexusEngine::BoundedFrameClient bounded(client,frameLimit);
        description.updateClient = &bounded; description.window.visible = !hidden;
        NexusEngine::NexusFramework framework;
        const auto code = framework.Run(description);
        if(code != 0 || !frameLimit) return code;
        if(bounded.Frames() < frameLimit) return failStartup("Host stopped before its requested GPU frames completed.");
        std::cout << "PASS: completed " << bounded.Frames() << " GPU frames and normal shutdown.\n";
        return 0;
    };
    std::error_code pathError;
    std::shared_ptr<const NexusEngine::ProjectContext> context;
    if(!projectArgument.empty() && !std::filesystem::is_directory(projectArgument, pathError)) {
        auto opened = NexusEngine::ProjectService::Open(projectArgument);
        if(!opened) return failStartup(opened.error().GetMessageText());
        auto compatible = NexusEngine::CheckProjectEngine(opened->Descriptor().engine,
            {NEXUS_HOST_ENGINE_ASSOCIATION, NEXUS_HOST_ENGINE_ID, NEXUS_HOST_ENGINE_VERSION});
        if(!compatible) return failStartup(compatible.error().GetMessageText());
        context = std::make_shared<const NexusEngine::ProjectContext>(std::move(*opened));
        pathError.clear();
    }
    auto projectRoot = context ? std::optional<std::filesystem::path>{context->Root()} : projectArgument.empty()
        ? NexusEngine::FindProjectRoot(std::filesystem::current_path(pathError))
        : std::optional<std::filesystem::path>{std::filesystem::absolute(projectArgument, pathError)};
    if(pathError) return failStartup("Cannot resolve project path: " + pathError.message());
    if(!projectRoot && projectArgument.empty()) {
        std::wstring executable(32768, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if(length > 0 && length < executable.size()) {
            executable.resize(length);
            projectRoot = NexusEngine::FindProjectRoot(std::filesystem::path(executable).parent_path());
        }
    }
    if(!projectRoot) return failStartup("Cannot locate the project folder (Config/Project.settings and Resources/Assets).");
    *projectRoot = std::filesystem::weakly_canonical(*projectRoot, pathError);
    if(pathError) return failStartup("Cannot access the project folder: " + pathError.message());
    std::wstring executable(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if(length == 0 || length >= executable.size()) return failStartup("Cannot locate engine executable.");
    executable.resize(length);
    auto engineRoot = engineArgument.empty() ? NexusEngine::FindEngineRoot(std::filesystem::path(executable).parent_path())
        : std::optional<std::filesystem::path>{std::filesystem::absolute(engineArgument, pathError)};
    if(pathError || !engineRoot || !std::filesystem::is_directory(*engineRoot / "Shaders"))
        return failStartup("Cannot locate engine Shaders. Specify --engine <root>.");
    const NexusEngine::ApplicationPaths paths{*projectRoot, *engineRoot, context};
    const auto project = NexusEngine::LoadProjectSettings(*projectRoot, context.get());
    if(!project) return failStartup(project.error().GetMessageText());
    const auto assetResult = NexusEngine::ResolveProjectPath(*projectRoot, std::filesystem::path{NexusEngine::ConvertString(project->assetPath)});
    if(!assetResult) return failStartup(assetResult.error().GetMessageText());
    const auto assetRoot = *assetResult;
    const auto levelReference = levelArgument.empty()
        ? std::filesystem::path{NexusEngine::ConvertString(project->startupLevel)} : levelArgument;
    const auto levelResult = NexusEngine::ResolveProjectPath(*projectRoot, levelReference);
    if(!levelResult) return failStartup(levelResult.error().GetMessageText());
    const auto levelPath = *levelResult;

    if(gameMode) {
        std::optional<NexusEngine::CompiledPostProcessGraph> renderPipeline;
        const auto pipelineFile=(context ? context->Config() : *projectRoot/"Config")/"RenderPipeline.json";
        if(std::filesystem::exists(pipelineFile)) {
            auto graph=NexusEngine::LoadPostProcessGraph(pipelineFile);
            if(!graph) return failStartup(graph.error().GetMessageText());
            NexusEngine::ResolvePostProcessGraphShaders(*graph,*projectRoot);
            auto compiled=graph->Compile();
            if(!compiled) return failStartup(compiled.error().GetMessageText());
            renderPipeline=std::move(*compiled);
        }
        NexusEngine::GameApplication game(NEXUS_PROJECT_REFLECTION, paths);
        if(!game.SetGameLevelSetup(NEXUS_PROJECT_SETUP)) return failStartup("Cannot configure game systems.");
        auto result = game.Load(levelPath, assetRoot, NEXUS_PROJECT_STARTUP_LOADER(assetRoot));
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
        gameDesc.window.title = NexusEngine::ConvertString(project->name) + L" - NexusEngine Runtime";
        gameDesc.graphics.assetDirectory = assetRoot;
        gameDesc.graphics.shaderDirectory = *engineRoot / "Shaders";
        if(renderPipeline) {
            gameDesc.graphics.postProcess=renderPipeline->settings;
            gameDesc.graphics.screenEffects=std::move(renderPipeline->effects);
        }
        gameDesc.renderer = &renderer;
        gameDesc.updateClient = &game;
        return run(gameDesc,game);
    }

#ifndef NEXUS_GAME_HOST
	NexusEngine::EditorApplication editor(NEXUS_PROJECT_REFLECTION, paths,
	    NEXUS_PROJECT_STARTUP_LOADER(assetRoot));
	if(!editor.SetGameLevelSetup(NEXUS_PROJECT_SETUP)) return failStartup("Cannot configure game systems.");
	if(context && (!engineManifestArgument.empty() || std::filesystem::exists(context->Config() / "BuildTargets.json"))) {
		auto binding = NexusEngine::ProjectBuild::Binding(*context, std::filesystem::path(executable), engineManifestArgument);
		if(!binding) return failStartup(binding.error().GetMessageText());
		auto standalone = std::make_shared<NexusEngine::StandaloneSession>(context,*binding);
		editor.AddEditorExtension([standalone,context](NexusEngine::EditorWorkspace& workspace) {
			workspace.Commands().Register("project.openVisualStudio", {[context,&workspace] {
				auto opened = NexusEngine::SourceCodeEditor::Open(*context);
				if(!opened) workspace.Report(std::unexpected(opened.error()));
			}});
			workspace.Menu().RegisterTool({"Open game in Visual Studio", "project.openVisualStudio"});
			NexusEngine::RegisterStandaloneCommands(workspace,standalone);
		});
	}
	NexusEngine::PrimitiveRenderer primitiveRenderer;
	editor.SetShadowSystem(&primitiveRenderer.GetShadows());
	editor.SetOpticalSystem(&primitiveRenderer.GetOptics());
	editor.SetMaterialRenderer(&primitiveRenderer);


	NexusEngine::FrameworkDesc desc;
	desc.graphics.assetDirectory = assetRoot;
	desc.graphics.shaderDirectory = *engineRoot / "Shaders";
	// CPU Sceneを先に構築し、Renderer初期化へ選択済みAssetと姿勢を接続する。
	// GPU Resourceの所有はRenderer/Graphics側に残すため、SceneにはDeviceを渡さない。
	startup.SetStatus(L"Loading level and project assets...");
	auto sceneResult = editor.CreateScene(desc.graphics.assetDirectory, levelPath);
	if(!sceneResult) {
		return failStartup(sceneResult.error().GetMessageText());
	}
	if(context) {
		auto registryFile = NexusEngine::ProjectRegistry::DefaultFile();
		if(registryFile) {
			auto registered = NexusEngine::ProjectRegistry(*registryFile).Register(*context);
			if(!registered) NEXUS_LOG_WARNING("Project", registered.error().GetMessageText());
		} else NEXUS_LOG_WARNING("Project", registryFile.error().GetMessageText());
	}
	primitiveRenderer.SetRenderScene(&editor.GetRenderScene());
	primitiveRenderer.SetModelInput(&editor.GetModelAsset(), &editor.GetModelPose());
	desc.mode = NexusEngine::FrameworkMode::Editor;
	desc.deferWindowUntilReady = !hidden;
	desc.startupProgress = [&startup](NexusEngine::FrameworkStartupStage stage) {
		using Stage = NexusEngine::FrameworkStartupStage;
		switch(stage) {
		case Stage::Window: startup.SetStatus(L"Creating the editor window..."); break;
		case Stage::Graphics: startup.SetStatus(L"Initializing graphics..."); break;
		case Stage::Renderer: startup.SetStatus(L"Preparing shaders and scene rendering..."); break;
		case Stage::Extension: startup.SetStatus(L"Preparing editor tools..."); break;
		case Stage::FirstFrame:
			startup.Close(); NEXUS_LOG_INFO("Startup","Editor first frame ready; startup display closed."); break;
		}
	};
	desc.renderer = &primitiveRenderer;
	desc.renderExtension = &editor;
	desc.updateClient = &editor;
	desc.messageHandler = [](void* userData, HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
		return static_cast<NexusEngine::EditorApplication*>(userData)->HandleWindowMessage(
			window, message, static_cast<uintptr_t>(wParam), static_cast<intptr_t>(lParam));
	};
	desc.messageHandlerUserData = &editor;
	return run(desc,editor);
#else
    return failStartup("Game host could not start.");
#endif
}
