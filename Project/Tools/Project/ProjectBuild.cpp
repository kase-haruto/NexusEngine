#include "ProjectBuild.h"
#include "Runtime/Project/ProjectJson.h"
#include "Runtime/Project/ProjectSerializer.h"
#include "Runtime/Application/ApplicationPaths.h"
#include "Platform/FileSystem/AtomicFile.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <chrono>
#include <thread>
#include <array>
#include <vector>
#include <span>
#include <algorithm>
#include <string_view>
#include <stdexcept>
#include <set>
#include <cwctype>

namespace NexusEngine {
namespace {
using namespace ProjectJson;
Error Invalid(const std::string& message) { return Error(ErrorCategory::Framework, 192, message); }
void Replace(std::string& text, const std::string& token, const std::string& value) {
    size_t offset = 0; while((offset = text.find(token, offset)) != text.npos) { text.replace(offset, token.size(), value); offset += value.size(); }
}
/** \brief 生成テンプレートの出力だけをprofile配置へ変換する。ProjectやBuildTargetsは変更しない */
std::string OutputReference(std::string value, const std::string& configuration, ProjectBuildProfile profile) {
    Replace(value, "{configuration}", configuration);
    if(profile == ProjectBuildProfile::NativeLivePatch) {
        std::ranges::replace(value, '\\', '/');
        constexpr std::string_view prefix = "Build/Bin/x64/";
        if(!value.starts_with(prefix)) throw std::runtime_error("NativeLivePatch requires the generated Build/Bin/x64 output layout.");
        value = "Build/Bin/LivePP/x64/" + value.substr(prefix.size());
    }
    return value;
}
/** \brief 安定Bootstrapの起動に必要な最小ファイル集合を、生成ホストと同じ配置契約で返す */
std::vector<std::filesystem::path> BootstrapFiles(const std::filesystem::path& assembly, std::string_view runtime) {
    auto config = assembly; config.replace_extension(".runtimeconfig.json");
    auto deps = assembly; deps.replace_extension(".deps.json");
    std::vector<std::filesystem::path> files{assembly, config, deps, assembly.parent_path().parent_path() / "nethost.dll"};
    if(runtime == "Mono") {
        const auto mono = assembly.parent_path().parent_path() / "Mono";
        files = {assembly, deps, mono / "bin/mono-2.0-sgen.dll", mono / "lib/mono/4.5/mscorlib.dll", mono / "etc/mono/config"};
    }
    const auto api = assembly.parent_path() / "Nexus.Managed.dll";
    if(std::filesystem::is_regular_file(api)) files.push_back(api); // 旧Phase 3 SDKの4ファイルstampも維持する。
    return files;
}
/** \brief 同じ配置契約のManaged成果物を照合し、別Assemblyや改変済みのDLLを黙って起動しない */
Result<void> VerifyManagedFiles(const ProjectContext& project, const Json& entries, std::span<const std::filesystem::path> files) {
    if(!entries.is_array() || entries.size() != files.size()) return std::unexpected(Invalid("Managed build stamp is incomplete. Close the host and rebuild."));
    for(size_t index = 0; index < files.size(); ++index) {
        const auto& item = entries.at(index); Keys(item, {"path", "sha256"});
        auto reference = Path(Text(item, "path"));
        if(reference.has_root_path()) return std::unexpected(Invalid("Managed stamp path must be relative."));
        auto path = ResolveProjectPath(project.Root(), reference); if(!path) return std::unexpected(path.error());
        if(!SameFilePath(*path, files[index])) return std::unexpected(Invalid("Managed stamp references another deployment."));
        auto digest = Sha256File(*path); if(!digest) return std::unexpected(digest.error());
        if(*digest != Text(item, "sha256")) return std::unexpected(Invalid("Managed deployment changed. Close the host and rebuild it."));
    }
    return {};
}
}
Result<ProjectBuildTargets> ProjectBuild::Targets(const ProjectContext& project, const std::string& configuration, ProjectBuildProfile profile) {
    auto compatibleModules = CheckProjectRuntimeModules(project.Descriptor()); if(!compatibleModules) return std::unexpected(compatibleModules.error());
    if(profile != ProjectBuildProfile::Standard && profile != ProjectBuildProfile::NativeLivePatch) return std::unexpected(Invalid("Unknown Build profile."));
    // 宣言できるModuleと現在の静的ホストが実行できるModuleを区別する。
    // 未対応Moduleを無視したホストのBuildや起動を成功として返さない。
    const auto cppCount = std::ranges::count_if(project.Descriptor().modules, [](const auto& item) { return item.language == ProjectModuleLanguage::Cpp; });
    const auto managedCount = std::ranges::count_if(project.Descriptor().modules, [](const auto& item) { return item.language == ProjectModuleLanguage::CSharp; });
    if(profile == ProjectBuildProfile::NativeLivePatch && (project.Descriptor().engine.association != "development" || configuration == "Release" || cppCount != 1))
        return std::unexpected(Invalid("NativeLivePatch requires development Debug/Develop and one Native Module."));
    if(configuration != "Debug" && configuration != "Develop" && configuration != "Release") return std::unexpected(Invalid("Unsupported configuration."));
    auto data = ProjectJson::Read(project.Config() / "BuildTargets.json"); if(!data) return std::unexpected(data.error());
    try {
        Keys(*data, {"schemaVersion", "editorProject", "runtimeProject", "editorExecutable", "runtimeExecutable", "nativeModule", "managedBootstrap", "gameScripts", "moduleProjects", "runtimeDependencies", "managedRuntime"});
        if(!data->at("schemaVersion").is_number_integer() || data->at("schemaVersion") != 1) throw std::runtime_error("Invalid Project build schema.");
        ProjectBuildTargets targets;
        if(data->contains("managedRuntime")) targets.managedRuntime = Text(*data, "managedRuntime");
        if(targets.managedRuntime != "Mono" && targets.managedRuntime != "CoreCLR") throw std::runtime_error("Unknown Managed Runtime backend.");
        const bool managed = managedCount > 0;
        if(managed != data->contains("managedBootstrap") || (!cppCount && data->contains("nativeModule")) ||
            (managedCount > 1 && !data->contains("gameScripts")))
            return std::unexpected(Invalid("Module language and BuildTargets backend differ. CSharp requires a Managed Bootstrap template."));
        if(!managed && data->contains("gameScripts")) return std::unexpected(Invalid("GameScripts requires a CSharp Module."));
        if(profile == ProjectBuildProfile::NativeLivePatch && !data->contains("nativeModule")) return std::unexpected(Invalid("NativeLivePatch requires an independent Native Game DLL."));
        for(const auto& [key, destination] : {std::pair{"editorProject", &targets.editorProject}, {"runtimeProject", &targets.runtimeProject},
            {"editorExecutable", &targets.editorExecutable}, {"runtimeExecutable", &targets.runtimeExecutable}}) {
            auto value = Text(*data, key); Replace(value, "{configuration}", configuration);
            if(key == std::string_view("editorExecutable") || key == std::string_view("runtimeExecutable")) value = OutputReference(value, configuration, profile);
            if(Path(value).has_root_path()) return std::unexpected(Invalid("Project build paths must be relative."));
            auto path = ResolveProjectPath(project.Root(), Path(value)); if(!path) return std::unexpected(path.error()); *destination = *path;
        }
        if(!SameFilePath(targets.runtimeExecutable.parent_path(), targets.editorExecutable.parent_path())) return std::unexpected(Invalid("Project hosts must share their Module deployment directory."));
        if(data->contains("nativeModule")) {
            auto value = OutputReference(Text(*data, "nativeModule"), configuration, profile);
            if(Path(value).has_root_path()) return std::unexpected(Invalid("Native Module build path must be relative."));
            auto path = ResolveProjectPath(project.Root(), Path(value)); if(!path) return std::unexpected(path.error());
            targets.nativeModule = *path;
            // 現在の生成ホストはGame.dllの隣接配置を契約にする。設定だけ変えて別DLLを照合させない。
            if(!SameFilePath(targets.nativeModule, targets.runtimeExecutable.parent_path() / "Game.dll") ||
               !SameFilePath(targets.nativeModule, targets.editorExecutable.parent_path() / "Game.dll"))
                return std::unexpected(Invalid("Native Module must be Game.dll beside both generated hosts."));
        }
        if(managed) {
            auto value = OutputReference(Text(*data, "managedBootstrap"), configuration, profile);
            if(Path(value).has_root_path()) return std::unexpected(Invalid("Managed Bootstrap build path must be relative."));
            auto path = ResolveProjectPath(project.Root(), Path(value)); if(!path) return std::unexpected(path.error());
            targets.managedBootstrap = *path;
            const std::filesystem::path adjacent = "Managed/Nexus.Managed.Bootstrap.dll";
            if(!SameFilePath(*path, targets.runtimeExecutable.parent_path() / adjacent) || !SameFilePath(*path, targets.editorExecutable.parent_path() / adjacent))
                return std::unexpected(Invalid("Managed Bootstrap must be in Managed/Nexus.Managed.Bootstrap.dll beside both hosts."));
            if(data->contains("gameScripts")) {
                auto script = OutputReference(Text(*data, "gameScripts"), configuration, profile);
                if(Path(script).has_root_path()) return std::unexpected(Invalid("GameScripts build path must be relative."));
                auto resolved = ResolveProjectPath(project.Root(), Path(script)); if(!resolved) return std::unexpected(resolved.error());
                if(!SameFilePath(*resolved, path->parent_path() / "GameScripts.dll")) return std::unexpected(Invalid("GameScripts.dll must be beside the stable Managed API."));
                targets.gameScripts = *resolved;
            }
        }
        std::set<std::string> mapped; std::vector<std::string> primary;
        for(const auto language : {ProjectModuleLanguage::Cpp, ProjectModuleLanguage::CSharp})
            for(const auto& module : project.Descriptor().modules) if(module.language == language) { primary.push_back(module.id); break; }
        if(cppCount && targets.nativeModule.empty()) {
            const auto legacy = std::ranges::find(project.Descriptor().modules, ProjectModuleLanguage::Cpp, &ProjectModuleDescriptor::language);
            if(!legacy->dependencies.empty()) throw std::runtime_error("Legacy static Game setup must precede dynamic Modules; use a Native DLL for dependent initialization.");
        }
        if(data->contains("moduleProjects")) {
            const auto& entries = data->at("moduleProjects"); if(!entries.is_array() || entries.size() > 256) throw std::runtime_error("Invalid Module build mappings.");
            for(const auto& item : entries) {
                Keys(item, {"id", "project", "output"}); auto id = Text(item, "id");
                const auto found = std::ranges::find(project.Descriptor().modules, id, &ProjectModuleDescriptor::id);
                if(found == project.Descriptor().modules.end() || std::ranges::find(primary, id) != primary.end() || !mapped.insert(id).second) throw std::runtime_error("Invalid or duplicate additional Module mapping.");
                auto reference = Path(Text(item, "project")), output = Path(OutputReference(Text(item, "output"), configuration, profile));
                if(reference.has_root_path() || output.has_root_path()) throw std::runtime_error("Module mapping must be relative.");
                auto source = ResolveProjectPath(project.Root(), reference), destination = ResolveProjectPath(project.Root(), output);
                if(!source) return std::unexpected(source.error()); if(!destination) return std::unexpected(destination.error());
                if(destination->extension() != ".dll") throw std::runtime_error("Module output must be a DLL.");
                if(SameFilePath(*source, targets.runtimeProject) || SameFilePath(*source, targets.editorProject)) throw std::runtime_error("A Module cannot recursively build its host project.");
                auto filename = destination->filename().wstring(); for(auto& character : filename) character = static_cast<wchar_t>(std::towlower(character));
                if(filename.starts_with(L"nexus.") || filename.starts_with(L"lpp_") || filename == L"nethost.dll" || filename == L"dxcompiler.dll" || filename == L"dxil.dll") throw std::runtime_error("Module output uses an Engine / Hosting / Live++ reserved filename.");
                if((!targets.nativeModule.empty() && SameFilePath(*source, project.Root() / "GameModule.vcxproj")) || (!targets.gameScripts.empty() && SameFilePath(*source, project.Root() / "GameScripts.csproj"))) throw std::runtime_error("Additional Module cannot reuse the primary Module project.");
                const auto expected = targets.runtimeExecutable.parent_path() / (found->language == ProjectModuleLanguage::Cpp ? "" : "Managed");
                if(source->extension() != (found->language == ProjectModuleLanguage::Cpp ? ".vcxproj" : ".csproj") || !SameFilePath(destination->parent_path(), expected)) throw std::runtime_error("Module project type / deployment directory mismatch.");
                if(SameFilePath(*destination, targets.nativeModule) || SameFilePath(*destination, targets.gameScripts) || SameFilePath(*destination, targets.managedBootstrap) || SameFilePath(*destination, expected / "Nexus.Managed.dll")) throw std::runtime_error("Module output conflicts with a primary or stable assembly.");
                for(const auto& existing : targets.modules) if(SameFilePath(existing.output, *destination) || SameFilePath(existing.project, *source)) throw std::runtime_error("Module projects / outputs must be unique.");
                targets.modules.push_back({id, found->language, *source, *destination});
            }
        }
        for(const auto& module : project.Descriptor().modules)
            if(std::ranges::find(primary, module.id) == primary.end() && !mapped.contains(module.id)) throw std::runtime_error("Missing additional Module mapping: " + module.id);
        if(data->contains("runtimeDependencies")) {
            const auto& entries = data->at("runtimeDependencies"); if(!entries.is_array() || entries.size() > 256) throw std::runtime_error("Invalid runtime dependency list.");
            for(const auto& entry : entries) {
                if(!entry.is_string()) throw std::runtime_error("Runtime dependency must be a path.");
                auto value = entry.get<std::string>(); Replace(value, "{configuration}", configuration); const auto reference = Path(value);
                if(reference.has_root_path() || reference.extension() != ".dll") throw std::runtime_error("Runtime dependency must be a relative DLL.");
                auto file = ResolveProjectPath(project.Root(), reference); if(!file) return std::unexpected(file.error());
                for(const auto& existing : targets.runtimeDependencies) if(SameFilePath(existing.filename(), file->filename())) throw std::runtime_error("Duplicate runtime dependency filename.");
                targets.runtimeDependencies.push_back(*file);
            }
        }
        return targets;
    } catch(const std::exception& exception) { return std::unexpected(Invalid(exception.what())); }
}

////////////////////////////
// 単一Gameターゲットだけを起動し、終了までworkerで待機する
////////////////////////////
Result<std::filesystem::path> ProjectBuild::Build(const ProjectContext& project, const EngineInstallation& engine,
    ProjectHostKind kind, const std::filesystem::path& log, std::stop_token stop, const ProjectBuildOptions& options) {
    auto compatible = CheckProjectEngine(project.Descriptor().engine, engine.identity); if(!compatible) return std::unexpected(compatible.error());
    auto targets = Targets(project, engine.configuration, options.profile); if(!targets) return std::unexpected(targets.error());
    for(const auto& module : targets->modules) for(const auto& dependency : engine.runtimeFiles)
        if(SameFilePath(module.output, targets->runtimeExecutable.parent_path() / dependency.filename())) return std::unexpected(Invalid("Module output conflicts with an Engine runtime dependency."));
    if(options.profile == ProjectBuildProfile::NativeLivePatch) {
        if(!options.livePPDirectory.is_absolute() || !std::filesystem::is_regular_file(options.livePPDirectory / "API/x64/LPP_API_x64_CPP.h") ||
            !std::filesystem::is_regular_file(options.livePPDirectory / "Agent/x64/LPP_Agent_x64_CPP.dll")) return std::unexpected(Invalid("NativeLivePatch requires an explicit absolute Live++ SDK directory with API and Agent."));
    } else if(!options.livePPDirectory.empty()) return std::unexpected(Invalid("Live++ SDK path is only valid for NativeLivePatch."));
    auto output = kind == ProjectHostKind::Editor ? targets->editorExecutable : targets->runtimeExecutable;
    wchar_t system[32768]{};
    const auto length = GetSystemDirectoryW(system, static_cast<UINT>(std::size(system)));
    if(!length || length >= std::size(system)) return std::unexpected(Invalid("Cannot locate Windows PowerShell."));
    const auto powershell = std::filesystem::path(system) / "WindowsPowerShell/v1.0/powershell.exe";
    std::vector<std::wstring> arguments{L"-NoProfile", L"-ExecutionPolicy", L"Bypass", L"-File",
        (engine.sdk / "Tools/Project/Build-Game.ps1").wstring(), L"-ProjectFile", project.File().wstring(), L"-EngineManifest", engine.manifest.wstring(),
        L"-Kind", kind == ProjectHostKind::Editor ? L"Editor" : L"Runtime", L"-Configuration", Path(engine.configuration).wstring(),
        L"-BuildProfile", options.profile == ProjectBuildProfile::NativeLivePatch ? L"NativeLivePatch" : L"Standard"};
    if(options.profile == ProjectBuildProfile::NativeLivePatch) { arguments.push_back(L"-LivePPDirectory"); arguments.push_back(options.livePPDirectory.wstring()); }
    if(options.rebuild) arguments.push_back(L"-Rebuild");
    if(options.rebuildManaged) arguments.push_back(L"-RebuildManaged");
    auto child = Process::Start(powershell, arguments, project.Root(), true, log, true);
    if(!child) return std::unexpected(child.error());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(30);
    for(;;) {
        auto state = child->Poll(); if(!state) return std::unexpected(state.error());
        if(*state) {
            if(**state != 0) return std::unexpected(Invalid("Project build failed (" + std::to_string(**state) + "). Log: " + Utf8(log)));
            auto verified = Verify(project, engine, output, kind, options.profile); if(!verified) return std::unexpected(verified.error());
            return output;
        }
        if(stop.stop_requested() || std::chrono::steady_clock::now() > deadline) {
            auto terminated = child->Terminate(); if(!terminated) return std::unexpected(terminated.error());
            return std::unexpected(Invalid("Project build cancelled or timed out. Log: " + Utf8(log)));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

Result<void> ProjectBuild::Verify(const ProjectContext& project, const EngineInstallation& engine, const std::filesystem::path& executable, ProjectHostKind kind, ProjectBuildProfile profile) {
    auto stampFile = executable; stampFile += ".nxbuild";
    auto stamp = ProjectJson::Read(stampFile); if(!stamp) return std::unexpected(stamp.error());
    try {
        Keys(*stamp, {"schemaVersion", "projectId", "engineId", "engineVersion", "association", "buildId", "configuration", "sha256", "engineManifest", "hostKind", "nativeModule", "managedBootstrap", "gameScripts", "buildProfile", "moduleFiles", "managedFiles", "runtimeFiles", "deploymentHash", "projectHash", "targetsHash"});
        if(stamp->contains("projectHash")) { auto hash = Sha256File(project.File()); if(!hash || *hash != Text(*stamp, "projectHash")) return std::unexpected(Invalid("Project changed after Build. Rebuild the selected host.")); }
        if(stamp->contains("targetsHash")) { auto hash = Sha256File(project.Config() / "BuildTargets.json"); if(!hash || *hash != Text(*stamp, "targetsHash")) return std::unexpected(Invalid("BuildTargets changed after Build. Rebuild the selected host.")); }
        const auto recordedProfile = stamp->contains("buildProfile") ? Text(*stamp, "buildProfile") : "Standard";
        if(recordedProfile != (profile == ProjectBuildProfile::NativeLivePatch ? "NativeLivePatch" : "Standard")) return std::unexpected(Invalid("Game build profile differs from the requested operation."));
        if(!stamp->at("schemaVersion").is_number_integer() || stamp->at("schemaVersion") != 1 || Text(*stamp, "projectId") != project.Descriptor().projectId ||
            Text(*stamp, "engineId") != engine.identity.id || Text(*stamp, "engineVersion") != engine.identity.version || Text(*stamp, "association") != engine.identity.association ||
            Text(*stamp, "buildId") != engine.buildId || Text(*stamp, "configuration") != engine.configuration || Text(*stamp, "hostKind") != (kind == ProjectHostKind::Editor ? "Editor" : "Runtime"))
            return std::unexpected(Invalid("Game host was built for a different Project/Engine/configuration. Build it explicitly."));
        auto digest = Sha256File(executable); if(!digest) return std::unexpected(digest.error());
        if(*digest != Text(*stamp, "sha256")) return std::unexpected(Invalid("Game executable changed after its build stamp was written."));
        auto targets = Targets(project, engine.configuration, profile); if(!targets) return std::unexpected(targets.error());
        if(!targets->runtimeDependencies.empty()) {
            if(!stamp->contains("runtimeFiles")) return std::unexpected(Invalid("Runtime dependency stamp missing."));
            auto checked = VerifyManagedFiles(project, stamp->at("runtimeFiles"), targets->runtimeDependencies); if(!checked) return checked;
            for(const auto& file : targets->runtimeDependencies) { auto source = Sha256File(file), deployed = Sha256File(executable.parent_path() / file.filename()); if(!source || !deployed || *source != *deployed) return std::unexpected(Invalid("Runtime dependency deployment mismatch.")); }
        }
        if(!SameFilePath(executable, kind == ProjectHostKind::Editor ? targets->editorExecutable : targets->runtimeExecutable)) return std::unexpected(Invalid("Executable is outside the selected Build profile output."));
        if(!targets->modules.empty()) {
            if(!stamp->contains("moduleFiles") || stamp->at("moduleFiles").size() != targets->modules.size()) return std::unexpected(Invalid("Additional Module stamp missing."));
            for(const auto& module : targets->modules) {
                bool found = false;
                for(const auto& entry : stamp->at("moduleFiles")) if(Text(entry, "id") == module.id) {
                    auto path = ResolveProjectPath(project.Root(), Path(Text(entry, "path"))); auto hash = Sha256File(module.output);
                    if(found || !path || !SameFilePath(*path, module.output) || !hash || *hash != Text(entry, "sha256")) return std::unexpected(Invalid("Module stamp mismatch: " + module.id)); found = true;
                }
                if(!found) return std::unexpected(Invalid("Missing Module stamp: " + module.id));
            }
        }
        if(stamp->contains("managedFiles")) {
            for(const auto& entry : stamp->at("managedFiles")) { auto path = ResolveProjectPath(project.Root(), Path(Text(entry, "path"))); if(!path) return std::unexpected(path.error()); auto hash = Sha256File(*path); if(!hash || *hash != Text(entry, "sha256")) return std::unexpected(Invalid("Managed deployment changed.")); }
        }
        if(stamp->contains("deploymentHash")) { auto hash = Sha256File(executable.parent_path() / "Nexus.modules.json"); if(!hash || *hash != Text(*stamp, "deploymentHash")) return std::unexpected(Invalid("Module deployment manifest changed.")); }
        if(!targets->nativeModule.empty()) {
            if(!stamp->contains("nativeModule")) return std::unexpected(Invalid("Native Module build stamp is missing. Rebuild the selected Game host."));
            const auto& moduleStamp = stamp->at("nativeModule"); Keys(moduleStamp, {"path", "sha256"});
            const auto reference = Path(Text(moduleStamp, "path"));
            if(reference.has_root_path()) return std::unexpected(Invalid("Native Module stamp path must be relative."));
            auto path = ResolveProjectPath(project.Root(), reference); if(!path) return std::unexpected(path.error());
            if(!SameFilePath(*path, targets->nativeModule)) return std::unexpected(Invalid("Native Module build stamp references another DLL."));
            auto hash = Sha256File(*path); if(!hash) return std::unexpected(hash.error());
            if(*hash != Text(moduleStamp, "sha256")) return std::unexpected(Invalid("Native Game Module changed after its build stamp was written. Rebuild the Game host."));
        } else if(stamp->contains("nativeModule")) return std::unexpected(Invalid("Static Game host has an unexpected Native Module stamp."));
        if(!targets->managedBootstrap.empty()) {
            if(!stamp->contains("managedBootstrap"))
                return std::unexpected(Invalid("Managed Bootstrap build stamp is missing or incomplete. Rebuild the Game host."));
            const auto files = BootstrapFiles(targets->managedBootstrap, targets->managedRuntime);
            if(!targets->gameScripts.empty() && files.size() != (targets->managedRuntime == "Mono" ? 6u : 5u)) return std::unexpected(Invalid("Gameplay requires the stable Nexus.Managed.dll."));
            auto checked = VerifyManagedFiles(project, stamp->at("managedBootstrap"), files); if(!checked) return checked;
        } else if(stamp->contains("managedBootstrap")) return std::unexpected(Invalid("Native host has an unexpected Managed Bootstrap stamp."));
        if(!targets->gameScripts.empty()) {
            if(!stamp->contains("gameScripts")) return std::unexpected(Invalid("GameScripts build stamp is missing."));
            auto deps = targets->gameScripts; deps.replace_extension(".deps.json");
            auto metadata = targets->gameScripts; metadata.replace_extension(".metadata.json");
            const std::array files{targets->gameScripts, deps, metadata};
            auto checked = VerifyManagedFiles(project, stamp->at("gameScripts"), files); if(!checked) return checked;
        } else if(stamp->contains("gameScripts")) return std::unexpected(Invalid("Bootstrap-only host has an unexpected GameScripts stamp."));
        for(const auto& dependency : engine.runtimeFiles) {
            auto source = Sha256File(dependency), deployed = Sha256File(executable.parent_path() / dependency.filename());
            if(!source) return std::unexpected(source.error()); if(!deployed) return std::unexpected(deployed.error());
            if(*source != *deployed) return std::unexpected(Invalid("Game runtime dependency differs from the selected Engine: " + Utf8(dependency.filename())));
        }
        return {};
    } catch(const std::exception& exception) { return std::unexpected(Invalid(exception.what())); }
}

Result<EngineInstallation> ProjectBuild::Binding(const ProjectContext& project, const std::filesystem::path& executable, const std::filesystem::path& explicitManifest, ProjectBuildProfile profile) {
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
        auto verified = Verify(project, *engine, executable, ProjectHostKind::Editor, profile); if(!verified) return std::unexpected(verified.error());
    }
    return engine;
}

Result<Process> ProjectBuild::Launch(const ProjectContext& project, const EngineInstallation& engine, ProjectHostKind kind, bool checkOnly) {
    return Launch(project,engine,kind,ProjectLaunchOptions{.checkOnly = checkOnly});
}
Result<Process> ProjectBuild::Launch(const ProjectContext& project, const EngineInstallation& engine, ProjectHostKind kind, const ProjectLaunchOptions& options) {
    auto compatibleModules = CheckProjectRuntimeModules(project.Descriptor()); if(!compatibleModules) return std::unexpected(compatibleModules.error());
    // BuildTargets未生成のC# Projectを、旧データProject用ホストへfallbackさせない。
    if(std::ranges::any_of(project.Descriptor().modules, [](const auto& module) { return module.language == ProjectModuleLanguage::CSharp; }) &&
       !std::filesystem::exists(project.Config() / "BuildTargets.json"))
        return std::unexpected(Invalid("Declaration-only CSharp Project requires Managed Bootstrap host generation before launch."));
    if(options.frames > 10000 || (options.checkOnly && options.frames) || (!options.log.empty() && !options.log.is_absolute()))
        return std::unexpected(Invalid("Invalid Project launch options."));
    auto compatible = CheckProjectEngine(project.Descriptor().engine, engine.identity); if(!compatible) return std::unexpected(compatible.error());
    std::filesystem::path executable;
    if(std::filesystem::exists(project.Config() / "BuildTargets.json")) {
        auto targets = Targets(project, engine.configuration, options.build.profile); if(!targets) return std::unexpected(targets.error());
        executable = kind == ProjectHostKind::Editor ? targets->editorExecutable : targets->runtimeExecutable;
        auto valid = Verify(project, engine, executable, kind, options.build.profile); if(!valid) return std::unexpected(valid.error());
    } else {
        if(options.build.profile != ProjectBuildProfile::Standard) return std::unexpected(Invalid("Development profile requires generated BuildTargets.json."));
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
