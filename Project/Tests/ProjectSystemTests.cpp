#include "Runtime/Project/ProjectService.h"
#include "Runtime/Project/ProjectSerializer.h"
#include "Runtime/Project/ProjectJson.h"
#include "Runtime/Project/ModuleDeployment.h"
#include "Tools/Project/ProjectRegistry.h"
#include "Tools/Project/ProjectTemplates.h"
#include "Tools/Project/ProjectBuild.h"
#include "Runtime/Application/GameApplication.h"
#include "Runtime/Application/ApplicationPaths.h"
#include "Editor/Services/EditorSettings.h"
#include "Runtime/Scene/Components/CameraComponent.h"
#include "Platform/FileSystem/AtomicFile.h"
#include <atomic>
#include <fstream>
#include <iostream>
#include <thread>
#include <utility>
#include <algorithm>
#include <array>

using namespace NexusEngine;
namespace {
void Require(bool condition, const char* message) { if(!condition) throw std::runtime_error(message); }
std::string Bytes(const std::filesystem::path& file) { std::ifstream input(file, std::ios::binary); return {std::istreambuf_iterator<char>(input), {}}; }
void Put(const std::filesystem::path& file, const std::string& value) { std::ofstream output(file, std::ios::binary | std::ios::trunc); output << value; Require(static_cast<bool>(output), "Test write failed."); }
Result<void> InitialLevel(const ProjectContext& context) {
    Level level(context.Descriptor().name);
    auto loaded = level.LoadEmpty(); if(!loaded) return loaded;
    if(!level.CreateEntity("Camera").AddComponent<CameraComponent>()) return std::unexpected(Error(ErrorCategory::Scene, 1, "Camera fixture failed."));
    return level.Save(context.StartupLevel());
}
ProjectDescriptor Descriptor() {
    ProjectDescriptor value; value.name = "日本語 \"Game\"";
    value.paths = {"Content", "Worlds", "Code", "Settings"}; value.startupLevel = "Worlds/Start.level";
    return value;
}
void SerializerTests(const ProjectContext& context) {
    const auto saved = Bytes(context.File());
    auto read = ProjectSerializer::Load(context.File()); Require(read.has_value(), "Descriptor load failed.");
    Require(read->name == context.Descriptor().name && read->projectId == context.Descriptor().projectId, "Descriptor round-trip failed.");
    auto json = ProjectJson::Json::parse(saved);
    Require(read->modules.empty() && !json.contains("modules"), "Legacy Project acquired an implicit Game Module.");
    // 言語追加が既存データの配置やProject IDを変更しないことを検証する。
    auto hybrid = context.Descriptor();
    hybrid.modules = {{"gameplay", "Gameplay", ProjectModuleLanguage::CSharp, "Code/Gameplay", ProjectModuleType::Game},
        {"combat", "Native Combat", ProjectModuleLanguage::Cpp, "Code/NativeCombat", ProjectModuleType::Game}};
    const auto modulesFile = context.Root() / "Modules.nxproject";
    Require(ProjectSerializer::Save(modulesFile, hybrid).has_value(), "Hybrid metadata save failed.");
    auto modulesRead = ProjectSerializer::Load(modulesFile);
    Require(modulesRead && modulesRead->modules.size() == 2 && modulesRead->modules[0].id == "gameplay" &&
        modulesRead->modules[0].language == ProjectModuleLanguage::CSharp && modulesRead->modules[1].language == ProjectModuleLanguage::Cpp &&
        modulesRead->modules[1].path == "Code/NativeCombat" && modulesRead->modules[1].type == ProjectModuleType::Game,
        "Hybrid Module metadata was lost.");
    Require(modulesRead->projectId == hybrid.projectId && modulesRead->paths.assets == hybrid.paths.assets &&
        modulesRead->startupLevel == hybrid.startupLevel, "Language addition changed common Project data.");
    Require(ProjectContext::Resolve(modulesFile, hybrid).has_value(), "Declaration-only Module requires generated code.");
    const auto modulesJson = ProjectJson::Json::parse(Bytes(modulesFile));
    // 公開名と配置を変更しても安定IDを再生成しない。
    hybrid.modules[0].name = "Renamed Gameplay"; hybrid.modules[0].path = "Code/RenamedGameplay";
    Require(ProjectSerializer::Save(modulesFile, hybrid).has_value(), "Renamed Module save failed.");
    modulesRead = ProjectSerializer::Load(modulesFile);
    Require(modulesRead && modulesRead->modules[0].id == "gameplay" && modulesRead->modules[0].name == "Renamed Gameplay",
        "Module rename changed its stable ID.");
    const auto moduleBytes = Bytes(modulesFile);
    auto duplicate = hybrid; duplicate.modules[1].id = duplicate.modules[0].id;
    Require(!ProjectSerializer::Save(modulesFile, duplicate) && Bytes(modulesFile) == moduleBytes, "Duplicate Module ID replaced the descriptor.");
    for(const char* path : {"../Outside", "C:/Outside", "Code/../Outside", "Code\\Game", "CON"}) {
        auto invalidModule = hybrid; invalidModule.modules[0].path = path;
        Require(!ProjectSerializer::Save(modulesFile, invalidModule) && Bytes(modulesFile) == moduleBytes, "Invalid Module path was saved.");
    }
    // 未知言語・用途・項目を既定値へ読み替えると後続保存で情報が失われるため拒否する。
    for(const auto& [key, value] : {std::pair{"language", "Lua"}, std::pair{"type", "Editor"}, std::pair{"future", "value"}}) {
        auto invalidModule = modulesJson; invalidModule["modules"][0][key] = value;
        Put(modulesFile, invalidModule.dump()); Require(!ProjectSerializer::Load(modulesFile), "Unknown Module metadata accepted.");
    }
    for(const auto& value : {ProjectJson::Json(nullptr), ProjectJson::Json::object(), ProjectJson::Json("Cpp")}) {
        auto invalidModules = modulesJson; invalidModules["modules"] = value;
        Put(modulesFile, invalidModules.dump()); Require(!ProjectSerializer::Load(modulesFile), "Invalid modules container accepted.");
    }
    Require(Bytes(context.File()) == saved, "Module checks modified the legacy Project.");
    const auto badFile = context.Root() / "Malformed.nxproject";
    for(const auto& malformed : {std::string("{}"), std::string("{\"schemaVersion\":1,\"schemaVersion\":1}"), std::string("[]"), saved + "false"}) {
        Put(badFile, malformed); Require(!ProjectSerializer::Load(badFile), "Malformed JSON was accepted.");
    }
    for(auto version : {ProjectJson::Json(2), ProjectJson::Json(1.0), ProjectJson::Json("1"), ProjectJson::Json(-1)}) {
        auto invalid = json; invalid["schemaVersion"] = version; Put(badFile, invalid.dump());
        Require(!ProjectSerializer::Load(badFile), "Unsupported schema was accepted.");
    }
    auto invalid = json; invalid["future"] = 1; Put(badFile, invalid.dump());
    Require(!ProjectSerializer::Load(badFile), "Unknown fields would be lost.");
    auto duplicated = saved;
    const auto schema = duplicated.find("\"schemaVersion\": 1"); Require(schema != std::string::npos, "Schema fixture missing.");
    duplicated.replace(schema, std::string("\"schemaVersion\": 1").size(), "\"schemaVersion\":1,\"schemaVersion\":1");
    Put(badFile, duplicated); Require(!ProjectSerializer::Load(badFile), "Duplicate schema key accepted.");
    for(const char* path : {"../Outside", "/Absolute", "C:/Absolute", "C:relative", "Assets/../Escape", "Assets\\Escape", "Assets/.. /Escape", "CON", "AUX.txt", "Data/*"}) {
        auto descriptor = context.Descriptor(); descriptor.paths.assets = path;
        Require(!ProjectSerializer::Save(context.File(), descriptor), "Invalid path was saved.");
        Require(Bytes(context.File()) == saved, "Invalid save replaced the original.");
    }
    auto descriptor = context.Descriptor(); descriptor.startupLevel = "Worlds2/Outside.level";
    Require(!ProjectContext::Resolve(context.File(), descriptor), "Startup Level escaped Level root.");
    descriptor = context.Descriptor(); descriptor.engine.association = "installed";
    Require(!ProjectSerializer::Validate(descriptor), "Unpinned Installed Engine was accepted.");
    descriptor.engine.version = "1.0.0"; Require(ProjectSerializer::Validate(descriptor).has_value(), "Pinned engine rejected.");
    descriptor.engine.association = "unknown"; Require(!ProjectSerializer::Validate(descriptor), "Unknown association accepted.");
    Require(!ProjectSerializer::Save(context.File(), context.Descriptor(), false), "Create-only descriptor overwrote a file.");
    Require(Bytes(context.File()) == saved, "Create-only write changed a file.");
    Put(badFile, std::string(1024 * 1024 + 1, ' ')); Require(!ProjectSerializer::Load(badFile), "Oversized input accepted.");
    std::string deep(40, '['); deep += '0'; deep += std::string(40, ']'); Put(badFile, deep);
    Require(!ProjectSerializer::Load(badFile), "Excessive JSON depth accepted.");
    auto malformedUtf8 = saved;
    const auto name = malformedUtf8.find("\"name\": \""); Require(name != std::string::npos, "Name fixture missing.");
    malformedUtf8.insert(name + std::string("\"name\": \"").size(), 1, static_cast<char>(-1));
    Put(badFile, malformedUtf8); Require(!ProjectSerializer::Load(badFile), "Invalid UTF-8 accepted.");
}
/** \brief 保存互換性と依存検証のソース。実行判断はユーザーへ委ねる */
void ModuleDependencyTests(const ProjectContext& context) {
    auto descriptor = context.Descriptor();
    descriptor.modules = {{"Gameplay", "Gameplay", ProjectModuleLanguage::CSharp, "Code/Game", ProjectModuleType::Game, {"Native"}},
                          {"Native", "Native", ProjectModuleLanguage::Cpp, "Code/Native", ProjectModuleType::Game}};
    auto order = ProjectSerializer::DependencyOrder(descriptor);
    Require(order && *order == std::vector<size_t>{1, 0}, "Dependency must precede the initial-language ordering.");
    Require(CheckProjectRuntimeModules(descriptor).has_value(), "Supported Hybrid dependency was rejected.");
    const auto file = context.Root() / "Dependencies.nxproject";
    Require(ProjectSerializer::Save(file, descriptor).has_value(), "Dependency save failed.");
    const auto before = Bytes(file); auto loaded = ProjectSerializer::Load(file);
    Require(loaded && loaded->modules[0].dependencies == std::vector<std::string>{"Native"}, "Dependency roundtrip lost IDs.");
    for(const auto& dependencies : {std::vector<std::string>{"Missing"}, {"Gameplay"}, {"Native", "Native"}}) {
        auto invalid = descriptor; invalid.modules[0].dependencies = dependencies;
        Require(!ProjectSerializer::Save(file, invalid) && Bytes(file) == before, "Invalid graph replaced the descriptor.");
    }
    auto cycle = descriptor; cycle.modules[1].dependencies = {"Gameplay"};
    Require(!ProjectSerializer::DependencyOrder(cycle), "Cyclic dependency accepted.");
    auto reverse = descriptor; reverse.modules[0].dependencies.clear(); reverse.modules[1].dependencies = {"Gameplay"};
    Require(ProjectSerializer::Validate(reverse) && !CheckProjectRuntimeModules(reverse), "Declaration-only initialization order was executed.");
    for(const auto& value : {ProjectJson::Json(nullptr), ProjectJson::Json("Native"), ProjectJson::Json::array({1})}) {
        auto document = ProjectJson::Json::parse(before); document["modules"][0]["dependencies"] = value;
        Put(file, document.dump()); Require(!ProjectSerializer::Load(file), "Malformed dependency container accepted.");
    }
}
/** \brief Buildしない計画検証のソース。profileの配置・拒否条件と通常版の互換性を確認する */
void BuildProfileTests(const std::filesystem::path& root) {
    auto descriptor = Descriptor(); descriptor.startupLevel = descriptor.paths.levels + "/Startup.level";
    descriptor.modules = {{"Native", "Native", ProjectModuleLanguage::Cpp, "Code/Native", ProjectModuleType::Game}};
    auto created = ProjectService::Create(root / "Profiles", descriptor, InitialLevel); Require(created.has_value(), "Profile fixture creation failed.");
    const auto targetsFile = created->Config() / "BuildTargets.json";
    const ProjectJson::Json targets{{"schemaVersion", 1}, {"editorProject", "GameEditor.vcxproj"}, {"runtimeProject", "GameRuntime.vcxproj"},
        {"editorExecutable", "Build/Bin/x64/{configuration}/GameEditor.exe"}, {"runtimeExecutable", "Build/Bin/x64/{configuration}/GameRuntime.exe"},
        {"nativeModule", "Build/Bin/x64/{configuration}/Game.dll"}};
    Put(targetsFile, targets.dump()); const auto before = Bytes(targetsFile);
    auto standard = ProjectBuild::Targets(*created, "Debug");
    auto native = ProjectBuild::Targets(*created, "Debug", ProjectBuildProfile::NativeLivePatch);
    Require(standard && native && SameFilePath(standard->runtimeExecutable, created->Root() / "Build/Bin/x64/Debug/GameRuntime.exe") &&
        SameFilePath(native->runtimeExecutable, created->Root() / "Build/Bin/LivePP/x64/Debug/GameRuntime.exe"), "Profile outputs were mixed.");
    Require(SameFilePath(standard->editorProject, native->editorProject) && Bytes(targetsFile) == before, "Planning rewrote the shared project.");
    Require(!ProjectBuild::Targets(*created, "Release", ProjectBuildProfile::NativeLivePatch), "Release patch profile accepted.");
    Require(!ProjectBuild::Targets(*created, "Debug", static_cast<ProjectBuildProfile>(99)), "Unknown profile accepted.");
    auto bad = targets; bad["nativeModule"] = "Other/Game.dll"; Put(targetsFile, bad.dump());
    Require(!ProjectBuild::Targets(*created, "Debug", ProjectBuildProfile::NativeLivePatch), "Unrelated profile output accepted.");
}
/** \brief 複数Moduleの通常計画・衝突・配布inventory破損を検査するソース。実行はユーザーに委ねる */
void ModuleDeliveryTests(const std::filesystem::path& root) {
    auto descriptor = Descriptor();
    descriptor.modules = {{"Native", "Native", ProjectModuleLanguage::Cpp, "Code/Native", ProjectModuleType::Game},
        {"Other", "Other", ProjectModuleLanguage::Cpp, "Code/Other", ProjectModuleType::Game, {"Native"}},
        {"Scripts", "Scripts", ProjectModuleLanguage::CSharp, "Code/Scripts", ProjectModuleType::Game, {"Other"}},
        {"ExtraScripts", "ExtraScripts", ProjectModuleLanguage::CSharp, "Code/ExtraScripts", ProjectModuleType::Game, {"Scripts"}}};
    auto project = ProjectService::Create(root / "Delivery", descriptor, InitialLevel); Require(project.has_value(), "Delivery Project creation failed.");
    ProjectJson::Json targets{{"schemaVersion", 1}, {"runtimeProject", "GameRuntime.vcxproj"}, {"editorProject", "GameEditor.vcxproj"},
        {"runtimeExecutable", "Build/Bin/x64/{configuration}/GameRuntime.exe"}, {"editorExecutable", "Build/Bin/x64/{configuration}/GameEditor.exe"},
        {"nativeModule", "Build/Bin/x64/{configuration}/Game.dll"}, {"managedBootstrap", "Build/Bin/x64/{configuration}/Managed/Nexus.Managed.Bootstrap.dll"},
        {"gameScripts", "Build/Bin/x64/{configuration}/Managed/GameScripts.dll"},
        {"moduleProjects", ProjectJson::Json::array({{{"id", "Other"}, {"project", "Other.vcxproj"}, {"output", "Build/Bin/x64/{configuration}/Other.dll"}},
            {{"id", "ExtraScripts"}, {"project", "ExtraScripts.csproj"}, {"output", "Build/Bin/x64/{configuration}/Managed/ExtraScripts.dll"}}})}};
    Put(project->Config() / "BuildTargets.json", targets.dump());
    auto plan = ProjectBuild::Targets(*project, "Debug"); Require(plan && plan->modules.size() == 2, "Multi-language Module plan rejected.");
    Require(!ProjectBuild::Targets(*project, "Debug", ProjectBuildProfile::NativeLivePatch), "Multi-DLL patch profile accepted.");
    auto conflict = targets; conflict["moduleProjects"][0]["output"] = "Build/Bin/x64/{configuration}/gAmE.dll";
    Put(project->Config() / "BuildTargets.json", conflict.dump()); Require(!ProjectBuild::Targets(*project, "Debug"), "Case-insensitive output collision accepted.");
    for(const auto& reserved : {"nethost.dll", "dxcompiler.dll", "LPP_Agent_x64_CPP.dll", "Nexus.Managed.dll"}) {
        auto invalid = targets; invalid["moduleProjects"][0]["output"] = std::string("Build/Bin/x64/{configuration}/") + reserved;
        Put(project->Config() / "BuildTargets.json", invalid.dump()); Require(!ProjectBuild::Targets(*project, "Debug"), "Reserved runtime filename accepted as a Module output.");
    }
    auto recursive = targets; recursive["moduleProjects"][0]["project"] = "GameRuntime.vcxproj";
    Put(project->Config() / "BuildTargets.json", recursive.dump()); Require(!ProjectBuild::Targets(*project, "Debug"), "Recursive host Project accepted as a Module input.");
    const auto package = root / "Package", bin = package / "Bin", executable = bin / "GameRuntime.exe";
    std::filesystem::create_directories(bin); std::filesystem::create_directories(package / "EngineData/Shaders");
    Put(executable, "inert executable fixture"); Put(package / "Game.nxproject", "inert Project fixture"); Put(bin / "Nexus.modules.json", "{}");
    ProjectJson::Json entries = ProjectJson::Json::array();
    for(const auto& relative : {"Bin/GameRuntime.exe", "Game.nxproject", "Bin/Nexus.modules.json"}) {
        auto hash = Sha256File(package / relative); Require(hash.has_value(), "Package fixture hash failed."); entries.push_back({{"path", relative}, {"sha256", *hash}});
    }
    ProjectJson::Json manifest{{"schemaVersion", 1}, {"project", "Game.nxproject"}, {"engineData", "EngineData"}, {"dotnetRoot", ""},
        {"configuration", "Debug"}, {"engineId", "fixture"}, {"engineVersion", "1"}, {"files", entries}};
    Put(bin / "NexusGame.package.json", manifest.dump()); Require(LoadPackageStartup(executable, "fixture", "1").has_value(), "Valid Package inventory rejected.");
    Require(!LoadPackageStartup(executable, "other", "1"), "Foreign Engine Package accepted.");
    auto missing = manifest; missing["files"].erase(missing["files"].begin()); Put(bin / "NexusGame.package.json", missing.dump());
    Require(!LoadPackageStartup(executable), "Incomplete startup inventory accepted.");
    Put(bin / "NexusGame.package.json", manifest.dump()); Put(executable, "changed fixture"); Require(!LoadPackageStartup(executable), "Changed Package accepted.");
    Put(executable, "inert executable fixture");
    auto escaped = manifest; escaped["project"] = "../Foreign.nxproject"; Put(bin / "NexusGame.package.json", escaped.dump());
    Require(!LoadPackageStartup(executable), "Package root escape accepted.");
}
/** \brief 設定だけ変更された古い成果物を拒否する。fixtureは通常ファイルで、DLL / Gameを起動しない */
void BuildTargetsStampTests(const std::filesystem::path& root) {
    auto descriptor = Descriptor(); descriptor.engine.version = "0.1.0";
    descriptor.modules = {{"Native", "Native", ProjectModuleLanguage::Cpp, "Code/Native", ProjectModuleType::Game}};
    auto project = ProjectService::Create(root / "Stamp", descriptor, InitialLevel); Require(project.has_value(), "Stamp Project creation failed.");
    ProjectJson::Json targets{{"schemaVersion", 1}, {"runtimeProject", "GameRuntime.vcxproj"}, {"editorProject", "GameEditor.vcxproj"},
        {"runtimeExecutable", "Build/Bin/x64/{configuration}/GameRuntime.exe"}, {"editorExecutable", "Build/Bin/x64/{configuration}/GameEditor.exe"},
        {"nativeModule", "Build/Bin/x64/{configuration}/Game.dll"}};
    const auto config = project->Config() / "BuildTargets.json";
    Put(config, targets.dump());
    const auto bin = project->Root() / "Build/Bin/x64/Debug"; std::filesystem::create_directories(bin);
    const auto executable = bin / "GameRuntime.exe";
    Put(executable, "inert host bytes"); Put(bin / "Game.dll", "inert module bytes");
    auto targetHash = Sha256File(config), projectHash = Sha256File(project->File()), hostHash = Sha256File(executable), moduleHash = Sha256File(bin / "Game.dll");
    Require(targetHash && projectHash && hostHash && moduleHash, "Cannot hash stamp fixtures.");
    EngineInstallation engine; engine.identity = project->Descriptor().engine; engine.configuration = "Debug"; engine.buildId = "fixture-build";
    ProjectJson::Json stamp{{"schemaVersion", 1}, {"projectId", project->Descriptor().projectId}, {"engineId", engine.identity.id},
        {"engineVersion", engine.identity.version}, {"association", engine.identity.association}, {"buildId", engine.buildId}, {"configuration", "Debug"},
        {"hostKind", "Runtime"}, {"engineManifest", "fixture.nxengine"}, {"sha256", *hostHash}, {"projectHash", *projectHash}, {"targetsHash", *targetHash},
        {"nativeModule", {{"path", "Build/Bin/x64/Debug/Game.dll"}, {"sha256", *moduleHash}}}};
    auto stampFile = executable; stampFile += ".nxbuild"; Put(stampFile, stamp.dump());
    Require(ProjectBuild::Verify(*project, engine, executable).has_value(), "Matching BuildTargets stamp rejected.");
    targets["editorProject"] = "OtherEditor.vcxproj"; Put(config, targets.dump());
    auto changed = ProjectBuild::Verify(*project, engine, executable);
    Require(!changed && changed.error().GetMessageText().find("BuildTargets changed") != std::string::npos, "Changed BuildTargets accepted with an old stamp.");
}
void CreationTests(const std::filesystem::path& root, const ProjectContext& context) {
    for(const auto* directory : {&context.Assets(), &context.Source(), &context.Config()})
        Require(std::filesystem::is_regular_file(*directory / ".gitkeep"), "Git clone would lose an empty required Directory.");
    auto existing = ProjectService::Create(context.Root(), Descriptor(), InitialLevel);
    Require(!existing, "Existing root was overwritten.");
    const auto count = std::distance(std::filesystem::directory_iterator(root), std::filesystem::directory_iterator{});
    auto failed = ProjectService::Create(root / "Failed", Descriptor(), [](const ProjectContext&) -> Result<void> { return std::unexpected(Error(ErrorCategory::General, 1, "Expected failure.")); });
    Require(!failed && !std::filesystem::exists(root / "Failed"), "Initializer rollback failed.");
    auto thrown = ProjectService::Create(root / "Thrown", Descriptor(), [](const ProjectContext&) -> Result<void> { throw std::runtime_error("Expected exception."); });
    Require(!thrown && !std::filesystem::exists(root / "Thrown"), "Exception rollback failed.");
    Require(count == std::distance(std::filesystem::directory_iterator(root), std::filesystem::directory_iterator{}), "Staging was leaked.");
    Require(!ProjectService::Create(root / "NoStartup", Descriptor(), [](const ProjectContext&) -> Result<void> { return {}; }), "Missing startup Level was accepted.");
    auto custom = Descriptor(); custom.paths.source = "Generated/GameSource";
    auto shared = ProjectService::Create(root / "SharedSource", custom, InitialLevel);
    Require(shared && Bytes(shared->Root() / ".gitignore").find("/Generated/") == std::string::npos, "Shared source directory was ignored by Git.");
    const auto missing = context.Source().parent_path() / "MovedSource";
    std::filesystem::rename(context.Source(), missing);
    Require(!ProjectService::Open(context.File()), "Missing source folder was accepted.");
    std::filesystem::rename(missing, context.Source());
    const auto outside = root / "Outside"; std::filesystem::create_directory(outside);
    const auto link = context.Root() / "Escape";
    std::error_code error; std::filesystem::create_directory_symlink(outside, link, error);
    if(!error) {
        auto escaped = context.Descriptor(); escaped.paths.assets = "Escape";
        Require(!ProjectContext::Resolve(context.File(), escaped), "Symlink escaped Project root.");
    } else std::cout << "SKIP: symlink creation requires Windows privileges.\n";
}
/** \brief 共通生成経路のHybrid両初期言語と既存単言語テンプレートを検証する。Buildは行わない */
void HybridTemplateTests(const std::filesystem::path& root) {
    auto templates = ProjectTemplates::List(std::filesystem::absolute("Project/Templates")); Require(templates.has_value(), "Cannot list Templates.");
    EngineInstallation engine; engine.identity = Descriptor().engine;
    for(const auto language : {ProjectModuleLanguage::Cpp, ProjectModuleLanguage::CSharp}) {
        const auto found = std::ranges::find(*templates, "hybrid-gameplay", &ProjectTemplate::id); Require(found != templates->end(), "Hybrid Template missing.");
        auto descriptor = Descriptor(); descriptor.startupLevel = descriptor.paths.levels + "/Startup.level";
        const auto directory = root / (language == ProjectModuleLanguage::Cpp ? "Hybrid-Cpp" : "Hybrid-CSharp");
        auto created = ProjectTemplates::Create(directory, descriptor, *found, engine, language);
        Require(created && created->Descriptor().modules.size() == 2 && created->Descriptor().modules.front().language == language, "Hybrid initial language fixed the Project language.");
        auto order = ProjectSerializer::DependencyOrder(created->Descriptor());
        Require(order && created->Descriptor().modules[order->front()].id == "NativeCombat" && CheckProjectRuntimeModules(created->Descriptor()), "Hybrid dependency was lost during generation.");
        Require(std::filesystem::is_regular_file(created->Source() / "NativeCombat/Main.cpp") && std::filesystem::is_regular_file(created->Source() / "Game/PlayerController.cs"), "Hybrid did not generate both Modules.");
        const auto native = Bytes(created->Root() / "GameModule.vcxproj");
        Require(native.find("@NATIVE_GUID@") == native.npos && native.find("Code\\NativeCombat") != native.npos, "Native Project substitutions were lost.");
        Require(Bytes(created->Root() / "ManagedBootstrap.vcxproj").find("GameModule.vcxproj") != std::string::npos, "Hybrid host dependency is incomplete.");
        Require(Bytes(created->Source() / "NativeCombat/Main.cpp").find(created->Descriptor().projectId) != std::string::npos, "DLL Project identity does not match.");
    }
    for(const auto& entry : std::array{std::pair{"native-module", ProjectModuleLanguage::Cpp}, std::pair{"managed-gameplay", ProjectModuleLanguage::CSharp}}) {
        const auto found = std::ranges::find(*templates, entry.first, &ProjectTemplate::id); Require(found != templates->end(), "Existing Template missing.");
        auto descriptor = Descriptor(); descriptor.startupLevel = descriptor.paths.levels + "/Startup.level";
        auto created = ProjectTemplates::Create(root / entry.first, descriptor, *found, engine, entry.second);
        Require(created && created->Descriptor().modules.size() == 1, "Single-language generation changed unexpectedly.");
    }
}
void EngineTests() {
    ProjectEngineRequirement current{"development", "engine-a", "0.1.0-dev"};
    Require(CheckProjectEngine({"development", "engine-a", ""}, current).has_value(), "Explicit Development tracking rejected.");
    Require(CheckProjectEngine(current, current).has_value(), "Exact engine rejected.");
    Require(!CheckProjectEngine({"development", "engine-b", ""}, current), "Different Engine ID accepted.");
    Require(!CheckProjectEngine({"development", "engine-a", "2.0.0"}, current), "Different version accepted.");
    Require(!CheckProjectEngine({"installed", "engine-a", "0.1.0-dev"}, current), "Installed project silently used Development.");
}
void EditorSettingsTests(const ProjectContext& context) {
    EditorSettingsStore settings; settings.Load(context.Root(), &context);
    Require(settings.project.name == context.Descriptor().name && settings.project.assetPath == "Content", "Editor ignored Descriptor settings.");
    settings.editor.grid = false; Require(settings.SaveEditor(), "Custom Config write failed.");
    Require(std::filesystem::is_regular_file(context.Config() / "Editor.settings"), "Editor saved into hardcoded Config.");
    auto changed = context.Descriptor(); changed.engine.version = "0.2.0-dev";
    Require(ProjectSerializer::Save(context.File(), changed).has_value(), "External descriptor update failed.");
    settings.project.name = "Edited Project"; Require(settings.SaveProject(), "Descriptor settings write failed.");
    auto read = ProjectSerializer::Load(context.File()); Require(read && read->name == "Edited Project" && read->engine.version == "0.2.0-dev", "Settings save lost Engine metadata.");
    Require(!std::filesystem::exists(context.Config() / "Project.settings"), "Descriptor settings were duplicated into legacy config.");
}
void RegistryTests(const std::filesystem::path& root, ProjectContext context) {
    const auto file = root / "User/Recent.json"; ProjectRegistry registry(file);
    Require(registry.Read()->empty(), "Missing registry was not empty.");
    Require(registry.Register(context).has_value(), "Project registration failed.");
    Require(registry.Register(context).has_value() && registry.Read()->size() == 1, "Repeated registration duplicated a Project.");
    const auto movedRoot = root / L"移動後 Game";
    // 移動対象はこのテストで作成したRootだけ。Engineや既存データは移動しない。
    Require(context.Root().parent_path() == root && movedRoot.parent_path() == root, "Unsafe test move.");
    const auto filename = context.File().filename(); std::filesystem::rename(context.Root(), movedRoot);
    Require(registry.Read()->front().state == RecentProjectState::Missing, "Moved project was not detected.");
    auto moved = ProjectService::Open(movedRoot / filename); Require(moved.has_value(), "Moved project failed to open.");
    Require(moved->Descriptor().projectId == context.Descriptor().projectId, "Move changed project ID.");
    Require(registry.Register(*moved).has_value() && registry.Read()->size() == 1, "Move did not replace missing record.");
    auto clone = ProjectService::Create(root / "Clone", moved->Descriptor(), InitialLevel);
    Require(clone && clone->Descriptor().projectId == moved->Descriptor().projectId, "Clone changed shared identity.");
    Require(registry.Register(*clone).has_value() && registry.Read()->size() == 2, "A live clone replaced another live checkout.");
    Require(registry.Remove(clone->File()).has_value() && std::filesystem::exists(clone->File()), "Clone entry removal deleted files.");
    auto second = ProjectService::Create(root / "Second", Descriptor(), InitialLevel); Require(second.has_value(), "Second Project failed.");
    std::atomic<bool> passed = true;
    {
        std::vector<std::jthread> threads;
        for(int index = 0; index < 6; ++index) threads.emplace_back([&, index] {
            for(int iteration = 0; iteration < 3; ++iteration) if(!registry.Register(index % 2 ? *moved : *second)) passed = false;
        });
    }
    Require(passed && registry.Read()->size() == 2, "Concurrent registry update lost data.");
    Require(registry.Remove(moved->File()).has_value() && registry.Read()->size() == 1 && std::filesystem::is_regular_file(moved->File()), "Removing recent entry deleted Project data.");
    Put(file, "{broken"); const auto broken = Bytes(file);
    Require(!registry.Read() && !registry.Register(*moved) && !registry.Remove(second->File()), "Corrupt Registry was overwritten.");
    Require(Bytes(file) == broken, "Corrupt Registry data was lost.");
    auto snapshot = std::make_shared<const ProjectContext>(*moved);
    GameApplication game({}, ApplicationPaths{snapshot->Root(), {}, snapshot});
    Require(game.Project() == snapshot.get(), "Runtime lost its Project context.");
    Require(game.Load(snapshot->StartupLevel(), snapshot->Assets()).has_value(), "Runtime could not load Project startup Level.");
    Require(game.Update(1.f / 60).has_value() && game.PrepareRender(1280, 720).has_value(), "Runtime Project update failed.");
}
}

int main() {
    try {
        Require(SameFilePath(L"C:/Engine/Build/../Engine.nxengine", L"c:\\engine\\engine.nxengine"), "Windows path separators/case were not normalized.");
        Require(SameFilePath(L"C:/Engine/", L"c:\\engine"), "Trailing path separator differs.");
        Require(!SameFilePath(L"C:/EngineA", L"C:/EngineB"), "Different paths compared equal.");
        auto uuid = CreateUuid(); Require(uuid.has_value(), "Cannot generate test ID.");
        const auto root = std::filesystem::absolute("Generated/Phase2Validation/" + *uuid);
        std::filesystem::create_directories(root);
        std::filesystem::create_directories(root / "Installed/Data/Shaders");
        const auto dataRoot = FindEngineRoot(root / "Installed/Bin");
        Require(dataRoot && SameFilePath(*dataRoot, root / "Installed/Data"), "Installed Data Root was replaced by an ancestor Development Engine.");
        auto created = ProjectService::Create(root / L"Game One 日本語", Descriptor(), InitialLevel);
        if(!created) throw std::runtime_error(created.error().GetMessageText());
        SerializerTests(*created); ModuleDependencyTests(*created); BuildProfileTests(root); ModuleDeliveryTests(root); BuildTargetsStampTests(root); CreationTests(root, *created); HybridTemplateTests(root); EngineTests(); EditorSettingsTests(*created); RegistryTests(root, *created);
        std::cout << "PASS: descriptor validation/round-trip, atomic writes, creation rollback, moved Unicode roots, explicit Engine checks, Editor settings, concurrent registry and Runtime startup.\n";
        return 0;
    } catch(const std::exception& exception) { std::cerr << "FAIL: " << exception.what() << '\n'; return 1; }
}
