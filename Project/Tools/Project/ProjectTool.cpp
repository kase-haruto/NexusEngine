#include "Runtime/Project/ProjectService.h"
#include "Runtime/Level/Level.h"
#include "Runtime/Scene/Components/CameraComponent.h"
#include "Runtime/Scene/Components/SkyRendererComponent.h"
#include "Foundation/Utility/String/ConvertString.h"
#include "ProjectRegistry.h"
#include "Tools/Engine/EngineRegistry.h"
#include "Tools/Engine/EnginePublisher.h"
#include "ProjectTemplates.h"
#include "ProjectBuild.h"
#include "ProjectMigration.h"
#include "Platform/FileSystem/AtomicFile.h"
#include <iostream>
#include <chrono>
#include <thread>

namespace {
std::string Utf8(const std::filesystem::path& path) { const auto value = path.generic_u8string(); return {reinterpret_cast<const char*>(value.data()), value.size()}; }
int Fail(const NexusEngine::Error& error) { std::cerr << error.GetMessageText() << '\n'; return 1; }
}

int RunProjectTool(int argc, wchar_t** argv) {
    using namespace NexusEngine;
    if(argc < 3) {
        std::cout << "NexusProjectTool create <new-root> <name>\n"
                     "NexusProjectTool inspect <file.nxproject>\n"
                     "NexusProjectTool register|remove <file.nxproject> [registry.json]\n"
                     "NexusProjectTool list <registry.json>\n"
                     "NexusProjectTool engine-dev <repository> <configuration> [engine-registry.json]\n"
                     "NexusProjectTool engine-register <engine.nxengine> [engine-registry.json]\n"
                     "NexusProjectTool engine-remove|engine-default <engine.nxengine> [engine-registry.json]\n"
                     "NexusProjectTool engine-list <engine-registry.json>\n"
                     "NexusProjectTool engine-publish <development.nxengine> <new-root> <id> <version>\n"
                     "NexusProjectTool migration-plan <project.nxproject> <target.nxengine> <new-plan.json>\n"
                     "NexusProjectTool migration-apply <plan.json> --accept-api-risk\n"
                     "NexusProjectTool migration-restore <project.nxproject> <Recovery.json>\n"
                     "NexusProjectTool template-list <engine.nxengine>\n"
                     "NexusProjectTool create-template <new-root> <name> <engine.nxengine> <template-id>\n"
                     "NexusProjectTool build-editor|build-runtime|verify-editor|verify-runtime|open|run|check <file.nxproject> <engine-registry.json> [configuration]\n";
        return 1;
    }
    const std::wstring_view command = argv[1];
    if((command == L"engine-remove" || command == L"engine-default") && (argc == 3 || argc == 4)) {
        auto file = argc == 4 ? Result<std::filesystem::path>{argv[3]} : EngineRegistry::DefaultFile();
        if(!file) return Fail(file.error());
        EngineRegistry registry(*file);
        auto result = command == L"engine-remove" ? registry.Remove(argv[2]) : registry.SetDefault(argv[2]);
        return result ? 0 : Fail(result.error());
    }
    if(command == L"migration-plan" && argc == 5) {
        auto plan = ProjectMigration::Prepare(argv[2],argv[3]); if(!plan) return Fail(plan.error());
        auto saved = ProjectMigration::SavePlan(argv[4],*plan); if(!saved) return Fail(saved.error());
        std::cout << plan->previous.id << ' ' << plan->previous.version << " -> " << plan->target.id << ' ' << plan->target.version << '\n';
        for(const auto& note : plan->notes) std::cout << note << '\n';
        std::cout << "Review plan: " << Utf8(std::filesystem::absolute(argv[4])) << '\n'; return 0;
    }
    if(command == L"migration-apply" && argc == 4 && std::wstring_view(argv[3]) == L"--accept-api-risk") {
        auto plan = ProjectMigration::LoadPlan(argv[2]); if(!plan) return Fail(plan.error());
        auto result = ProjectMigration::Apply(*plan,true); if(!result) return Fail(result.error());
        std::cout << "Recovery: " << Utf8(*result) << '\n'; return 0;
    }
    if(command == L"migration-restore" && argc == 4) {
        auto result = ProjectMigration::Restore(argv[2],argv[3]); return result ? 0 : Fail(result.error());
    }
    if(command == L"engine-publish" && argc == 6) {
        auto source = EngineManifest::Load(argv[2]); if(!source) return Fail(source.error());
        auto id = CreateUuid(); if(!id) return Fail(id.error());
        const auto log = source->root / "Generated/EnginePublish" / (*id + ".log");
        auto result = EnginePublisher::Publish(*source, argv[3], {"installed", ConvertString(std::wstring(argv[4])), ConvertString(std::wstring(argv[5]))}, log);
        if(!result) { std::cerr << "Log: " << Utf8(log) << '\n'; return Fail(result.error()); }
        std::cout << Utf8(result->manifest) << '\n'; return 0;
    }
    if(command == L"engine-dev" && (argc == 4 || argc == 5)) {
        auto manifest = EngineManifest::DescribeDevelopment(argv[2], ConvertString(std::wstring(argv[3])));
        if(!manifest) return Fail(manifest.error());
        auto registryFile = argc == 5 ? Result<std::filesystem::path>{argv[4]} : EngineRegistry::DefaultFile();
        if(!registryFile) return Fail(registryFile.error());
        auto registered = EngineRegistry(*registryFile).Register(*manifest, true); if(!registered) return Fail(registered.error());
        std::cout << Utf8(*manifest) << '\n'; return 0;
    }
    if(command == L"engine-register" && (argc == 3 || argc == 4)) {
        auto registryFile = argc == 4 ? Result<std::filesystem::path>{argv[3]} : EngineRegistry::DefaultFile();
        if(!registryFile) return Fail(registryFile.error());
        auto registered = EngineRegistry(*registryFile).Register(argv[2], true); return registered ? 0 : Fail(registered.error());
    }
    if(command == L"engine-list" && argc == 3) {
        auto engines = EngineRegistry(argv[2]).Read(); if(!engines) return Fail(engines.error());
        for(const auto& engine : *engines) std::cout << engine.identity.id << '\t' << engine.identity.version << '\t' << engine.configuration << '\t' << (engine.installation ? "Available" : engine.error) << '\n';
        return 0;
    }
    if(command == L"template-list" && argc == 3) {
        auto engine = EngineManifest::Load(argv[2]); if(!engine) return Fail(engine.error());
        auto templates = ProjectTemplates::List(engine->templates); if(!templates) return Fail(templates.error());
        for(const auto& item : *templates) std::cout << item.id << '\t' << item.name << '\n';
        return 0;
    }
    if(command == L"create-template" && argc == 6) {
        auto engine = EngineManifest::Load(argv[4]); if(!engine) return Fail(engine.error());
        auto templates = ProjectTemplates::List(engine->templates); if(!templates) return Fail(templates.error());
        for(const auto& item : *templates) if(item.id == ConvertString(std::wstring(argv[5]))) {
            ProjectDescriptor descriptor; descriptor.name = ConvertString(std::wstring(argv[3])); descriptor.engine = engine->identity;
            auto created = ProjectTemplates::Create(argv[2], std::move(descriptor), item, *engine);
            if(!created) return Fail(created.error()); std::cout << Utf8(created->File()) << '\n'; return 0;
        }
        std::cerr << "Template ID was not found.\n"; return 1;
    }
    if((command == L"build-editor" || command == L"build-runtime" || command == L"verify-editor" || command == L"verify-runtime" || command == L"open" || command == L"run" || command == L"check") && (argc == 4 || argc == 5)) {
        auto project = ProjectService::Open(argv[2]); if(!project) return Fail(project.error());
        auto engine = EngineRegistry(argv[3]).Resolve(project->Descriptor().engine, argc == 5 ? ConvertString(std::wstring(argv[4])) : "Debug");
        if(!engine) return Fail(engine.error());
        const auto kind = command == L"build-editor" || command == L"verify-editor" || command == L"open" ? ProjectHostKind::Editor : ProjectHostKind::Runtime;
        if(command == L"verify-editor" || command == L"verify-runtime") {
            auto targets = ProjectBuild::Targets(*project, engine->configuration); if(!targets) return Fail(targets.error());
            auto verified = ProjectBuild::Verify(*project, *engine, kind == ProjectHostKind::Editor ? targets->editorExecutable : targets->runtimeExecutable, kind);
            return verified ? 0 : Fail(verified.error());
        }
        if(std::filesystem::exists(project->Config() / "BuildTargets.json")) {
            auto id = CreateUuid(); if(!id) return Fail(id.error());
            auto built = ProjectBuild::Build(*project, *engine, kind, project->Root() / "Build/Logs" / (*id + ".log"));
            if(!built) return Fail(built.error());
        }
        if(command == L"build-editor" || command == L"build-runtime") return 0;
        auto process = ProjectBuild::Launch(*project, *engine, kind, command == L"check"); if(!process) return Fail(process.error());
        if(command != L"check") { std::cout << "Started PID " << process->Id() << '\n'; return 0; }
        for(;;) { auto state = process->Poll(); if(!state) return Fail(state.error()); if(*state) return static_cast<int>(**state); std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
    }
    if(command == L"create" && argc == 4) {
        ProjectDescriptor descriptor; descriptor.name = ConvertString(std::wstring(argv[3]));
        descriptor.engine = {NEXUS_HOST_ENGINE_ASSOCIATION, NEXUS_HOST_ENGINE_ID,
            std::string(NEXUS_HOST_ENGINE_ASSOCIATION) == "development" ? "" : NEXUS_HOST_ENGINE_VERSION};
        auto result = ProjectService::Create(argv[2], std::move(descriptor), [](const ProjectContext& context) -> Result<void> {
            // Engine defaults are explicit scene entities, matching the bundled templates.
            Level level(context.Descriptor().name);
            auto loaded = level.LoadEmpty(); if(!loaded) return loaded;
            if(!level.CreateEntity("Camera").AddComponent<CameraComponent>())
                return std::unexpected(Error(ErrorCategory::Scene, 166, "Cannot create initial Camera."));
            if(!level.CreateEntity("Sky").AddComponent<NexusEngine::SkyRendererComponent>())
                return std::unexpected(Error(ErrorCategory::Scene, 167, "Cannot create initial Sky."));
            return level.Save(context.StartupLevel());
        });
        if(!result) return Fail(result.error());
        std::cout << Utf8(result->File()) << '\n'; return 0;
    }
    if(command == L"inspect" && argc == 3) {
        auto result = ProjectService::Open(argv[2]); if(!result) return Fail(result.error());
        std::cout << result->Descriptor().name << '\n' << result->Descriptor().projectId << '\n' << Utf8(result->Root()) << '\n'; return 0;
    }
    if(command == L"list" && argc == 3) {
        auto projects = ProjectRegistry(argv[2]).Read(); if(!projects) return Fail(projects.error());
        for(const auto& project : *projects) std::cout << project.name << '\t' << Utf8(project.file) << '\t' << project.lastUsedUtc << '\t' << static_cast<int>(project.state) << '\n';
        return 0;
    }
    if((command == L"register" || command == L"remove") && (argc == 3 || argc == 4)) {
        auto file = argc == 4 ? Result<std::filesystem::path>{argv[3]} : ProjectRegistry::DefaultFile();
        if(!file) return Fail(file.error());
        ProjectRegistry registry(*file);
        if(command == L"remove") { auto removed = registry.Remove(argv[2]); return removed ? 0 : Fail(removed.error()); }
        auto context = ProjectService::Open(argv[2]); if(!context) return Fail(context.error());
        auto registered = registry.Register(*context); return registered ? 0 : Fail(registered.error());
    }
    std::cerr << "Invalid project command or arguments.\n"; return 1;
}

int wmain(int argc, wchar_t** argv) {
    try { return RunProjectTool(argc, argv); }
    catch(const std::exception& exception) { std::cerr << exception.what() << '\n'; return 1; }
}
