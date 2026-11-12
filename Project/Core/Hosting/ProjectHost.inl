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
#if defined(NEXUS_GAME_MODULE_LIVEPP_HOST) && !defined(NEXUS_PROJECT_NATIVE_MODULE) && !defined(NEXUS_PROJECT_MODULE_MANIFEST)
#error Module-only Live++ requires a Native Game Module host; use the normal profile for CSharp-only projects.
#endif

// engine
#include "Core/Framework/NexusFramework.h"
#include "Platform/Development/LiveCodingAgent.h"
#include "Core/Hosting/BoundedFrameClient.h"
#ifndef NEXUS_GAME_HOST
#include "Editor/Core/EditorApplication.h"
#include "Editor/Services/StandaloneSession.h"
#include "Editor/Services/StandaloneCommands.h"
#include "Editor/Services/ManagedReloadCommands.h"
#include "Editor/Services/ProjectDevelopmentCommands.h"
#include "Platform/Window/StartupSplash.h"
#endif
#include "Runtime/Application/ProjectSettings.h"
#include "Runtime/Project/ProjectService.h"
#include "Runtime/Project/ModuleDeployment.h"
#include "Runtime/Reflection/NativeComponentMetadata.h"
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
#include <algorithm>

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
#if defined(NEXUS_GAME_MODULE_LIVEPP_HOST)
        description.nativeGameModuleReload = true;
#endif
        NexusEngine::NexusFramework framework;
        const auto code = framework.Run(description);
        if(code != 0 || !frameLimit) return code;
        if(bounded.Frames() < frameLimit) return failStartup("Host stopped before its requested GPU frames completed.");
        std::cout << "PASS: completed " << bounded.Frames() << " GPU frames and normal shutdown.\n";
        return 0;
    };
    // 配布ホストは隣接manifestだけから共有Project / Engineデータを解決する。
    std::filesystem::path packagedDotnetRoot;
    {
        std::wstring image(32768, L'\0'); const auto size = GetModuleFileNameW(nullptr, image.data(), static_cast<DWORD>(image.size()));
        if(!size || size >= image.size()) return failStartup("Cannot locate package host."); image.resize(size);
        if(std::filesystem::exists(std::filesystem::path(image).parent_path() / "NexusGame.package.json")) {
            if(!gameMode || !projectArgument.empty() || !engineArgument.empty() || !levelArgument.empty()) return failStartup("Packaged host requires its fixed deployment arguments.");
            auto package = NexusEngine::LoadPackageStartup(std::filesystem::path(image), NEXUS_HOST_ENGINE_ID, NEXUS_HOST_ENGINE_VERSION); if(!package) return failStartup(package.error().GetMessageText());
            projectArgument = package->project; engineArgument = package->engineData; packagedDotnetRoot = package->dotnetRoot;
        }
    }
    std::error_code pathError;
    std::shared_ptr<const NexusEngine::ProjectContext> context;
    if(!projectArgument.empty() && !std::filesystem::is_directory(projectArgument, pathError)) {
        auto opened = NexusEngine::ProjectService::Open(projectArgument);
        if(!opened) return failStartup(opened.error().GetMessageText());
        auto compatible = NexusEngine::CheckProjectEngine(opened->Descriptor().engine,
            {NEXUS_HOST_ENGINE_ASSOCIATION, NEXUS_HOST_ENGINE_ID, NEXUS_HOST_ENGINE_VERSION});
        if(!compatible) return failStartup(compatible.error().GetMessageText());
        // 未対応の組合せを黙って無視せず、宣言言語と生成ホストのBackendを照合する。
        const auto cppCount = std::ranges::count_if(opened->Descriptor().modules, [](const auto& item) { return item.language == NexusEngine::ProjectModuleLanguage::Cpp; });
        const auto managedCount = std::ranges::count_if(opened->Descriptor().modules, [](const auto& item) { return item.language == NexusEngine::ProjectModuleLanguage::CSharp; });
        static_cast<void>(cppCount); static_cast<void>(managedCount);
        auto supportedModules = NexusEngine::CheckProjectRuntimeModules(opened->Descriptor());
        if(!supportedModules) return failStartup(supportedModules.error().GetMessageText());
#if !defined(NEXUS_PROJECT_NATIVE_MODULE) && !defined(NEXUS_PROJECT_MODULE_MANIFEST)
        if(cppCount && managedCount) return failStartup("Hybrid Project requires an explicit Native Game Module host.");
#endif
#if !defined(NEXUS_PROJECT_GAME_ASSEMBLY) && !defined(NEXUS_PROJECT_MODULE_MANIFEST)
        if(cppCount && managedCount) return failStartup("Hybrid Project requires a GameScripts assembly.");
#endif
        for(const auto& module : opened->Descriptor().modules) {
            if(module.language != NexusEngine::ProjectModuleLanguage::Cpp)
#if !defined(NEXUS_PROJECT_MANAGED_BOOTSTRAP) && !defined(NEXUS_PROJECT_MODULE_MANIFEST)
                return failStartup("CSharp Module requires a Managed Bootstrap host. This host cannot load it.");
#else
                continue;
#endif
        }
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
#ifdef NEXUS_PROJECT_NATIVE_MODULE
    // DLL配置は生成ホストが指定し、Project Root内の実行ファイル隣接配置へ解決する。
    // Moduleが認識しないProjectを、既定Gameへ置き換えて開始しない。
    if(!context) return failStartup("Native Module host requires a .nxproject.");
    const auto nativeDescriptor = std::ranges::find_if(context->Descriptor().modules, [](const auto& module) { return module.language == NexusEngine::ProjectModuleLanguage::Cpp; });
    if(nativeDescriptor == context->Descriptor().modules.end()) return failStartup("Native Module host requires a declared Cpp Game Module.");
    auto nativePath = NexusEngine::ResolveProjectPath(context->Root(), std::filesystem::path(executable).parent_path() / NEXUS_PROJECT_NATIVE_MODULE);
    if(!nativePath) return failStartup(nativePath.error().GetMessageText());
    NexusEngine::NativeGameModuleSettings nativeModuleSettings{*nativePath, NEXUS_HOST_ENGINE_ID, NEXUS_HOST_ENGINE_VERSION,
        context->Descriptor().projectId, nativeDescriptor->id};
#if defined(NEXUS_GAME_MODULE_LIVEPP_HOST)
    // Project定義のSourceを再利用する。配布用Rootに開発Sourceを要求しないよう明示profileに限定する。
    auto patchSource = NexusEngine::ResolveProjectPath(context->Root(), std::filesystem::path(NexusEngine::ConvertString(nativeDescriptor->path)));
    if(!patchSource) return failStartup(patchSource.error().GetMessageText());
    nativeModuleSettings.patchSourceDirectory = *patchSource;
    nativeModuleSettings.patchProjectDirectory = context->Root();
    nativeModuleSettings.livePatch = true;
    nativeModuleSettings.registerPatchRuntime = NexusEngine::LiveCodingAgent::RegisterGameRuntime;
    nativeModuleSettings.unregisterPatchRuntime = NexusEngine::LiveCodingAgent::UnregisterGameRuntime;
#endif
#endif
#ifdef NEXUS_PROJECT_MANAGED_BOOTSTRAP
    if(!context || std::ranges::none_of(context->Descriptor().modules, [](const auto& module) { return module.language == NexusEngine::ProjectModuleLanguage::CSharp; }))
        return failStartup("Managed Bootstrap host requires a declared CSharp Game Module.");
    const auto managedDirectory = std::filesystem::path(executable).parent_path();
    auto bootstrapPath = NexusEngine::ResolveProjectPath(context->Root(), managedDirectory / NEXUS_PROJECT_MANAGED_BOOTSTRAP);
    auto nethostPath = NexusEngine::ResolveProjectPath(context->Root(), managedDirectory / "nethost.dll");
    if(!bootstrapPath) return failStartup(bootstrapPath.error().GetMessageText());
    if(!nethostPath) return failStartup(nethostPath.error().GetMessageText());
    auto runtimeConfig = *bootstrapPath; runtimeConfig.replace_extension(".runtimeconfig.json");
    NexusEngine::CSharpRuntimeSettings csharpSettings{*nethostPath, runtimeConfig, *bootstrapPath, {}, {}};
#ifdef NEXUS_PROJECT_GAME_ASSEMBLY
    auto gameAssembly = NexusEngine::ResolveProjectPath(context->Root(), managedDirectory / NEXUS_PROJECT_GAME_ASSEMBLY);
    if(!gameAssembly) return failStartup(gameAssembly.error().GetMessageText());
    csharpSettings.gameAssembly = *gameAssembly;
#endif
#endif
    std::optional<NexusEngine::ModuleDeployment> deployment;
    if(context && std::filesystem::exists(std::filesystem::path(executable).parent_path() / "Nexus.modules.json")) {
        auto loaded = NexusEngine::LoadModuleDeployment(*context, std::filesystem::path(executable), NEXUS_HOST_ENGINE_ID, NEXUS_HOST_ENGINE_VERSION, packagedDotnetRoot);
        if(!loaded) return failStartup(loaded.error().GetMessageText()); deployment = std::move(*loaded);
#if defined(NEXUS_GAME_MODULE_LIVEPP_HOST)
        if(deployment->native.size() != 1) return failStartup("Live++ coordination requires one Native DLL; use Standard for multi-Module gameplay.");
        auto& settings = deployment->native.front();
        const auto module = std::ranges::find(context->Descriptor().modules, settings.moduleId, &NexusEngine::ProjectModuleDescriptor::id);
        auto source = NexusEngine::ResolveProjectPath(context->Root(), std::filesystem::path(NexusEngine::ConvertString(module->path)));
        if(!source) return failStartup(source.error().GetMessageText());
        settings.patchSourceDirectory = *source; settings.patchProjectDirectory = context->Root(); settings.livePatch = true;
        settings.registerPatchRuntime = NexusEngine::LiveCodingAgent::RegisterGameRuntime; settings.unregisterPatchRuntime = NexusEngine::LiveCodingAgent::UnregisterGameRuntime;
#endif
    }
#ifdef NEXUS_PROJECT_MODULE_MANIFEST
    if(!deployment) return failStartup("Build the Project to generate Nexus.modules.json.");
#endif
    if(context && !deployment) {
        for(const auto language : {NexusEngine::ProjectModuleLanguage::Cpp, NexusEngine::ProjectModuleLanguage::CSharp})
            if(std::ranges::count(context->Descriptor().modules, language, &NexusEngine::ProjectModuleDescriptor::language) > 1)
                return failStartup("Multiple Modules require the deployment manifest and an updated Project Host.");
    }
    auto engineRoot = engineArgument.empty() ? NexusEngine::FindEngineRoot(std::filesystem::path(executable).parent_path())
        : std::optional<std::filesystem::path>{std::filesystem::absolute(engineArgument, pathError)};
    if(pathError || !engineRoot || !std::filesystem::is_directory(*engineRoot / "Shaders"))
        return failStartup("Cannot locate engine Shaders. Specify --engine <root>.");
    const NexusEngine::ApplicationPaths paths{*projectRoot, *engineRoot, context};
    // Level読込より先にDLLの値Metadataだけを取得する。Editorはゲームobjectを生成・所有しない。
    auto reflection = NEXUS_PROJECT_REFLECTION;
    std::vector<NexusEngine::NativeGameModuleSettings> metadataModules;
    if(deployment) metadataModules = deployment->native;
#ifdef NEXUS_PROJECT_NATIVE_MODULE
    else {
        if(context) {
            const auto found = std::ranges::find(context->Descriptor().modules, nativeModuleSettings.moduleId, &NexusEngine::ProjectModuleDescriptor::id);
            if(found != context->Descriptor().modules.end()) {
                auto source = NexusEngine::ResolveProjectPath(context->Root(), NexusEngine::ProjectJson::Path(found->path));
                if(!source) return failStartup(source.error().GetMessageText()); nativeModuleSettings.sourceDirectory = *source;
            }
        }
        metadataModules.push_back(nativeModuleSettings);
    }
#endif
    if(!metadataModules.empty()) {
        auto metadata = NexusEngine::NativeComponentMetadata::Inspect(metadataModules);
        if(!metadata) return failStartup(metadata.error().GetMessageText()); reflection.nativeComponents = std::move(*metadata);
    }
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
        NexusEngine::GameApplication game(reflection, paths);
        if(!game.SetGameLevelSetup(NEXUS_PROJECT_SETUP)) return failStartup("Cannot configure game systems.");
#ifdef NEXUS_PROJECT_NATIVE_MODULE
        if(!game.SetNativeGameModule(nativeModuleSettings)) return failStartup("Cannot configure Native Game Module.");
#endif
#ifdef NEXUS_PROJECT_MANAGED_BOOTSTRAP
        if(!game.SetCSharpRuntime(csharpSettings)) return failStartup("Cannot configure CSharp Runtime hosting.");
#endif
        if(deployment) {
            if(!game.SetNativeGameModules(deployment->native)) return failStartup("Cannot configure Native deployments.");
            if(deployment->managed && !game.SetCSharpRuntime(*deployment->managed)) return failStartup("Cannot configure Managed deployment.");
        }
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
        // RuntimeはCPU候補を所有し、Hostだけが既存Graphicsの準備とGPU完了待ちを接続する。
        game.SetAssetPublisher([&game,&renderer](NexusEngine::GraphicsSystem& graphics,NexusEngine::IGraphicsRenderer& active,
            const std::shared_ptr<const NexusEngine::ModelAssetData>& asset,const NexusEngine::ModelInstance& pose,
            const std::function<void()>& commit)->NexusEngine::Result<void> {
            if(&active!=&renderer) return std::unexpected(NexusEngine::Error(NexusEngine::ErrorCategory::Resource,48,"Unexpected Runtime Renderer."));
            NexusEngine::PrimitiveRenderer prepared; prepared.SetModelInput(asset.get(),&pose);
            auto ready=graphics.PrepareModelResources(&prepared,renderer.CaptureModelResources()); if(!ready) return ready;
            auto idle=graphics.WaitForIdle(); if(!idle) return idle;
            renderer.CommitPreparedModel(prepared); commit();
            renderer.SetModelInput(&game.GetModelAsset(),&game.GetModelPose());
            return {};
        });
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
	NexusEngine::EditorApplication editor(reflection, paths,
	    NEXUS_PROJECT_STARTUP_LOADER(assetRoot));
	if(!editor.SetGameLevelSetup(NEXUS_PROJECT_SETUP)) return failStartup("Cannot configure game systems.");
#ifdef NEXUS_PROJECT_NATIVE_MODULE
    if(!editor.SetNativeGameModule(nativeModuleSettings)) return failStartup("Cannot configure Native Game Module.");
#endif
#ifdef NEXUS_PROJECT_MANAGED_BOOTSTRAP
    if(!editor.SetCSharpRuntime(csharpSettings)) return failStartup("Cannot configure CSharp Runtime hosting.");
#endif
    if(deployment) {
        if(!editor.SetNativeGameModules(deployment->native)) return failStartup("Cannot configure Native deployments.");
        if(deployment->managed && !editor.SetCSharpRuntime(*deployment->managed)) return failStartup("Cannot configure Managed deployment.");
    }
	if(context && (!engineManifestArgument.empty() || std::filesystem::exists(context->Config() / "BuildTargets.json"))) {
        NexusEngine::ProjectLaunchOptions standaloneOptions;
#if defined(NEXUS_GAME_MODULE_LIVEPP_HOST)
        standaloneOptions.build.profile = NexusEngine::ProjectBuildProfile::NativeLivePatch;
        standaloneOptions.build.livePPDirectory = NEXUS_GAME_MODULE_LIVEPP_DIRECTORY;
#endif
		auto binding = NexusEngine::ProjectBuild::Binding(*context, std::filesystem::path(executable), engineManifestArgument, standaloneOptions.build.profile);
		if(!binding) return failStartup(binding.error().GetMessageText());
		auto standalone = std::make_shared<NexusEngine::StandaloneSession>(context,*binding);
		editor.AddEditorExtension([standalone,context,standaloneOptions, engine = *binding](NexusEngine::EditorWorkspace& workspace) {
			workspace.Commands().Register("project.openVisualStudio", {[context,&workspace] {
				auto opened = NexusEngine::SourceCodeEditor::Open(*context);
				if(!opened) workspace.Report(std::unexpected(opened.error()));
			}});
			workspace.Menu().RegisterTool({"Open game in Visual Studio", "project.openVisualStudio"});
			NexusEngine::RegisterStandaloneCommands(workspace,standalone,standaloneOptions);
            NexusEngine::RegisterProjectDevelopmentCommands(workspace, context, engine, standaloneOptions.build);
		});
        if(deployment && deployment->managed && !deployment->managed->gameAssembly.empty()) {
            editor.AddEditorExtension([context, engine = *binding, settings = *deployment->managed](NexusEngine::EditorWorkspace& workspace) {
                NexusEngine::RegisterManagedReloadCommands(workspace, context, engine, settings);
            });
        }
#ifdef NEXUS_PROJECT_GAME_ASSEMBLY
        if(!deployment) editor.AddEditorExtension([context, engine = *binding, settings = csharpSettings](NexusEngine::EditorWorkspace& workspace) {
            NexusEngine::RegisterManagedReloadCommands(workspace, context, engine, settings);
        });
#endif
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
