#include "Runtime/Project/ProjectService.h"
#include "Runtime/Project/ProjectSerializer.h"
#include "Runtime/Project/ProjectJson.h"
#include "Tools/Project/ProjectRegistry.h"
#include "Runtime/Application/GameApplication.h"
#include "Runtime/Application/ApplicationPaths.h"
#include "Editor/Services/EditorSettings.h"
#include "Runtime/Scene/Components/CameraComponent.h"
#include "Platform/FileSystem/AtomicFile.h"
#include <atomic>
#include <fstream>
#include <iostream>
#include <thread>

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
        SerializerTests(*created); CreationTests(root, *created); EngineTests(); EditorSettingsTests(*created); RegistryTests(root, *created);
        std::cout << "PASS: descriptor validation/round-trip, atomic writes, creation rollback, moved Unicode roots, explicit Engine checks, Editor settings, concurrent registry and Runtime startup.\n";
        return 0;
    } catch(const std::exception& exception) { std::cerr << "FAIL: " << exception.what() << '\n'; return 1; }
}
