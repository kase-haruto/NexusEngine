#include "ProjectBuild.h"
#include "Runtime/Project/ProjectJson.h"
#include "Runtime/Application/ApplicationPaths.h"
#include "Platform/FileSystem/AtomicFile.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <chrono>
#include <thread>

namespace NexusEngine {
namespace {
using namespace ProjectJson;
Error Invalid(const std::string& message) { return Error(ErrorCategory::Framework, 192, message); }
void Replace(std::string& text, const std::string& token, const std::string& value) {
    size_t offset = 0; while((offset = text.find(token, offset)) != text.npos) { text.replace(offset, token.size(), value); offset += value.size(); }
}
}
Result<ProjectBuildTargets> ProjectBuild::Targets(const ProjectContext& project, const std::string& configuration) {
    if(configuration != "Debug" && configuration != "Develop" && configuration != "Release") return std::unexpected(Invalid("Unsupported configuration."));
    auto data = ProjectJson::Read(project.Config() / "BuildTargets.json"); if(!data) return std::unexpected(data.error());
    try {
        Keys(*data, {"schemaVersion", "editorProject", "runtimeProject", "editorExecutable", "runtimeExecutable"});
        if(!data->at("schemaVersion").is_number_integer() || data->at("schemaVersion") != 1) throw std::runtime_error("Invalid Project build schema.");
        ProjectBuildTargets targets;
        for(const auto& [key, destination] : {std::pair{"editorProject", &targets.editorProject}, {"runtimeProject", &targets.runtimeProject},
            {"editorExecutable", &targets.editorExecutable}, {"runtimeExecutable", &targets.runtimeExecutable}}) {
            auto value = Text(*data, key); Replace(value, "{configuration}", configuration);
            if(Path(value).has_root_path()) return std::unexpected(Invalid("Project build paths must be relative."));
            auto path = ResolveProjectPath(project.Root(), Path(value)); if(!path) return std::unexpected(path.error()); *destination = *path;
        }
        return targets;
    } catch(const std::exception& exception) { return std::unexpected(Invalid(exception.what())); }
}

////////////////////////////
// 単一Gameターゲットだけを起動し、終了までworkerで待機する
////////////////////////////
Result<std::filesystem::path> ProjectBuild::Build(const ProjectContext& project, const EngineInstallation& engine,
    ProjectHostKind kind, const std::filesystem::path& log, std::stop_token stop) {
    auto compatible = CheckProjectEngine(project.Descriptor().engine, engine.identity); if(!compatible) return std::unexpected(compatible.error());
    auto targets = Targets(project, engine.configuration); if(!targets) return std::unexpected(targets.error());
    auto output = kind == ProjectHostKind::Editor ? targets->editorExecutable : targets->runtimeExecutable;
    wchar_t system[32768]{};
    const auto length = GetSystemDirectoryW(system, static_cast<UINT>(std::size(system)));
    if(!length || length >= std::size(system)) return std::unexpected(Invalid("Cannot locate Windows PowerShell."));
    const auto powershell = std::filesystem::path(system) / "WindowsPowerShell/v1.0/powershell.exe";
    auto child = Process::Start(powershell, {L"-NoProfile", L"-ExecutionPolicy", L"Bypass", L"-File",
        (engine.sdk / "Tools/Project/Build-Game.ps1").wstring(), L"-ProjectFile", project.File().wstring(), L"-EngineManifest", engine.manifest.wstring(),
        L"-Kind", kind == ProjectHostKind::Editor ? L"Editor" : L"Runtime", L"-Configuration", Path(engine.configuration).wstring()}, project.Root(), true, log, true);
    if(!child) return std::unexpected(child.error());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(30);
    for(;;) {
        auto state = child->Poll(); if(!state) return std::unexpected(state.error());
        if(*state) {
            if(**state != 0) return std::unexpected(Invalid("Project build failed (" + std::to_string(**state) + "). Log: " + Utf8(log)));
            auto verified = Verify(project, engine, output, kind); if(!verified) return std::unexpected(verified.error());
            return output;
        }
        if(stop.stop_requested() || std::chrono::steady_clock::now() > deadline) {
            auto terminated = child->Terminate(); if(!terminated) return std::unexpected(terminated.error());
            return std::unexpected(Invalid("Project build cancelled or timed out. Log: " + Utf8(log)));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

Result<void> ProjectBuild::Verify(const ProjectContext& project, const EngineInstallation& engine, const std::filesystem::path& executable, ProjectHostKind kind) {
    auto stampFile = executable; stampFile += ".nxbuild";
    auto stamp = ProjectJson::Read(stampFile); if(!stamp) return std::unexpected(stamp.error());
    try {
        Keys(*stamp, {"schemaVersion", "projectId", "engineId", "engineVersion", "association", "buildId", "configuration", "sha256", "engineManifest", "hostKind"});
        if(!stamp->at("schemaVersion").is_number_integer() || stamp->at("schemaVersion") != 1 || Text(*stamp, "projectId") != project.Descriptor().projectId ||
            Text(*stamp, "engineId") != engine.identity.id || Text(*stamp, "engineVersion") != engine.identity.version || Text(*stamp, "association") != engine.identity.association ||
            Text(*stamp, "buildId") != engine.buildId || Text(*stamp, "configuration") != engine.configuration || Text(*stamp, "hostKind") != (kind == ProjectHostKind::Editor ? "Editor" : "Runtime"))
            return std::unexpected(Invalid("Game host was built for a different Project/Engine/configuration. Build it explicitly."));
        auto digest = Sha256File(executable); if(!digest) return std::unexpected(digest.error());
        if(*digest != Text(*stamp, "sha256")) return std::unexpected(Invalid("Game executable changed after its build stamp was written."));
        for(const auto& dependency : engine.runtimeFiles) {
            auto source = Sha256File(dependency), deployed = Sha256File(executable.parent_path() / dependency.filename());
            if(!source) return std::unexpected(source.error()); if(!deployed) return std::unexpected(deployed.error());
            if(*source != *deployed) return std::unexpected(Invalid("Game runtime dependency differs from the selected Engine: " + Utf8(dependency.filename())));
        }
        return {};
    } catch(const std::exception& exception) { return std::unexpected(Invalid(exception.what())); }
}

Result<EngineInstallation> ProjectBuild::Binding(const ProjectContext& project, const std::filesystem::path& executable, const std::filesystem::path& explicitManifest) {
    auto manifest = explicitManifest;
    if(manifest.empty()) {
        auto file = executable; file += ".nxbuild";
        auto stamp = ProjectJson::Read(file); if(!stamp) return std::unexpected(stamp.error());
        try { manifest = Path(Text(*stamp, "engineManifest")); }
        catch(const std::exception& exception) { return std::unexpected(Invalid(exception.what())); }
    }
    auto engine = EngineManifest::Load(manifest); if(!engine) return engine;
    auto compatible = CheckProjectEngine(project.Descriptor().engine, engine->identity); if(!compatible) return std::unexpected(compatible.error());
    if(std::filesystem::exists(project.Config() / "BuildTargets.json")) {
        auto verified = Verify(project, *engine, executable, ProjectHostKind::Editor); if(!verified) return std::unexpected(verified.error());
    }
    return engine;
}

Result<Process> ProjectBuild::Launch(const ProjectContext& project, const EngineInstallation& engine, ProjectHostKind kind, bool checkOnly) {
    return Launch(project,engine,kind,ProjectLaunchOptions{.checkOnly = checkOnly});
}
Result<Process> ProjectBuild::Launch(const ProjectContext& project, const EngineInstallation& engine, ProjectHostKind kind, const ProjectLaunchOptions& options) {
    if(options.frames > 10000 || (options.checkOnly && options.frames) || (!options.log.empty() && !options.log.is_absolute()))
        return std::unexpected(Invalid("Invalid Project launch options."));
    auto compatible = CheckProjectEngine(project.Descriptor().engine, engine.identity); if(!compatible) return std::unexpected(compatible.error());
    std::filesystem::path executable;
    if(std::filesystem::exists(project.Config() / "BuildTargets.json")) {
        auto targets = Targets(project, engine.configuration); if(!targets) return std::unexpected(targets.error());
        executable = kind == ProjectHostKind::Editor ? targets->editorExecutable : targets->runtimeExecutable;
        auto valid = Verify(project, engine, executable, kind); if(!valid) return std::unexpected(valid.error());
    } else {
        // 旧データProjectだけを組み込み型ホストへ接続する。Gameコードを黙って無視しない。
        for(const auto& entry : std::filesystem::recursive_directory_iterator(project.Source()))
            if(entry.is_regular_file() && entry.path().filename() != ".gitkeep" && entry.path().filename() != ".gitignore")
                return std::unexpected(Invalid("Game Source exists without BuildTargets.json. Generate/build a project-specific host."));
        executable = kind == ProjectHostKind::Editor ? engine.editor : engine.runtime;
    }
    std::vector<std::wstring> arguments{L"--project", project.File().wstring(), L"--engine", engine.data.wstring()};
    if(kind == ProjectHostKind::Editor) { arguments.push_back(L"--engine-manifest"); arguments.push_back(engine.manifest.wstring()); }
    if(kind == ProjectHostKind::Runtime) arguments.push_back(L"--game");
    if(options.checkOnly) { arguments.push_back(L"--game"); arguments.push_back(L"--check-game"); }
    if(options.hidden) arguments.push_back(L"--hidden");
    if(options.frames) { arguments.push_back(L"--frames"); arguments.push_back(std::to_wstring(options.frames)); }
    return Process::Start(executable, arguments, project.Root(), options.checkOnly || options.hidden, options.log);
}
}
