#pragma once
#include "ProjectContext.h"
#include "ProjectSerializer.h"
#include "ProjectJson.h"
#include "Runtime/Gameplay/GameModuleLoader.h"
#include "Runtime/Scripting/CSharpRuntimeSettings.h"
#include "Runtime/Application/ApplicationPaths.h"
#include "Platform/FileSystem/AtomicFile.h"
#include <set>
#include <algorithm>
#include <vector>
#include <optional>
#include <stdexcept>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * ModuleDeployment
 * - Build済みModuleの値manifestを既存Runtimeの設定へ変換する
 * - DLL・Assembly・Worldを所有せず、Project IDと配置を検査する
 *---------------------------------------------------------------------------------------*/
struct ModuleDeployment {
    std::vector<NativeGameModuleSettings> native; //< 依存順。所有権はGameRuntimeへ渡す
    std::optional<CSharpRuntimeSettings> managed; //< 全Managed Moduleは一つのScript世代へロードする
};
/** \brief 実行ファイル隣接のmanifestを検証し、共有World用のModule設定を作る */
inline Result<ModuleDeployment> LoadModuleDeployment(const ProjectContext& project, const std::filesystem::path& executable,
    const std::string& engineId, const std::string& version, const std::filesystem::path& dotnetRoot = {}) {
    using namespace ProjectJson;
    auto document = Read(executable.parent_path() / "Nexus.modules.json"); if(!document) return std::unexpected(document.error());
    try {
        Keys(*document, {"schemaVersion", "projectId", "native", "managedAssembly", "staticNative", "managedRuntime"});
        if(document->at("schemaVersion") != 1 || Text(*document, "projectId") != project.Descriptor().projectId || !document->at("native").is_array()) throw std::runtime_error("Invalid Module deployment identity.");
        auto order = ProjectSerializer::DependencyOrder(project.Descriptor()); if(!order) return std::unexpected(order.error());
        ModuleDeployment result; std::set<std::string> compiled;
        if(document->contains("staticNative")) {
            const auto& values = document->at("staticNative");
            if(!values.is_array() || values.size() > 1) throw std::runtime_error("Only the legacy primary Native Module may be compiled into its host.");
            const auto primary = std::ranges::find(project.Descriptor().modules, ProjectModuleLanguage::Cpp, &ProjectModuleDescriptor::language);
            for(const auto& id : values) {
                if(!id.is_string() || primary == project.Descriptor().modules.end() || id.get<std::string>() != primary->id || !compiled.insert(id.get<std::string>()).second) throw std::runtime_error("Invalid static Native declaration.");
                if(!primary->dependencies.empty()) throw std::runtime_error("Legacy host setup executes before dynamic Modules; move dependent setup into a Native DLL.");
            }
        }
        for(const auto index : *order) {
            const auto& module = project.Descriptor().modules[index]; if(module.language != ProjectModuleLanguage::Cpp) continue;
            const auto& entries = document->at("native"); const Json* found = nullptr;
            for(const auto& item : entries) {
                Keys(item, {"id", "path"}); if(Text(item, "id") == module.id) { if(found) throw std::runtime_error("Duplicate Native deployment."); found = &item; }
            }
            if(compiled.contains(module.id)) { if(found) throw std::runtime_error("Native Module cannot be both static and dynamic."); continue; } // 旧GameのSetup / ReflectionはHostに残す。
            if(!found) throw std::runtime_error("Missing Native deployment: " + module.id);
            auto reference = Path(Text(*found, "path")); if(reference != reference.filename() || reference.extension() != ".dll") throw std::runtime_error("Native Module must be a DLL beside its host.");
            auto path = ResolveProjectPath(project.Root(), executable.parent_path() / reference); if(!path) return std::unexpected(path.error());
            if(!std::filesystem::is_regular_file(*path)) throw std::runtime_error("Invalid Native module file.");
            for(const auto& existing : result.native) if(SameFilePath(existing.file, *path)) throw std::runtime_error("Duplicate Native DLL.");
            result.native.push_back({*path, engineId, version, project.Descriptor().projectId, module.id});
            auto source = ResolveProjectPath(project.Root(), Path(module.path)); if(!source) return std::unexpected(source.error());
            result.native.back().sourceDirectory = *source;
        }
        if(result.native.size() != document->at("native").size()) throw std::runtime_error("Deployment contains undeclared Native Modules.");
        const bool managed = std::ranges::any_of(project.Descriptor().modules, [](const auto& module) { return module.language == ProjectModuleLanguage::CSharp; });
        if(managed) {
            const auto bootstrap = executable.parent_path() / "Managed/Nexus.Managed.Bootstrap.dll";
            auto config = bootstrap; config.replace_extension(".runtimeconfig.json");
            const auto reference = Path(Text(*document, "managedAssembly", true));
            if(reference.has_root_path()) throw std::runtime_error("Managed deployment must be relative.");
            std::filesystem::path assembly;
            if(!reference.empty()) {
                auto path = ResolveProjectPath(project.Root(), executable.parent_path() / reference); if(!path) return std::unexpected(path.error()); assembly = *path;
                if(!SameFilePath(assembly, bootstrap.parent_path() / "GameScripts.dll")) throw std::runtime_error("Managed roots must share the stable API deployment directory.");
                auto roster = Read(bootstrap.parent_path() / "GameScripts.modules.json"); if(!roster) return std::unexpected(roster.error());
                const auto count = std::ranges::count(project.Descriptor().modules, ProjectModuleLanguage::CSharp, &ProjectModuleDescriptor::language);
                if(!roster->is_array() || roster->size() != static_cast<size_t>(count)) throw std::runtime_error("Managed Module roster differs from Project.");
                std::set<std::filesystem::path> files;
                for(const auto& entry : *roster) {
                    if(!entry.is_string()) throw std::runtime_error("Invalid Managed Module file.");
                    const auto file = Path(entry.get<std::string>());
                    if(file != file.filename() || file.extension() != ".dll" || !files.insert(file).second || !std::filesystem::is_regular_file(assembly.parent_path() / file)) throw std::runtime_error("Invalid Managed Module roster.");
                }
            } else if(std::ranges::count(project.Descriptor().modules, ProjectModuleLanguage::CSharp, &ProjectModuleDescriptor::language) > 1) throw std::runtime_error("Multiple Managed Modules require GameScripts.");
            result.managed = CSharpRuntimeSettings{executable.parent_path() / "nethost.dll", config, bootstrap, dotnetRoot, assembly};
            const auto backend = document->contains("managedRuntime") ? Text(*document, "managedRuntime") : "CoreCLR";
            if(backend == "Mono") result.managed->monoRoot = executable.parent_path() / "Mono";
            else if(backend != "CoreCLR") throw std::runtime_error("Unknown Managed Runtime backend.");
        } else if(document->contains("managedAssembly") && !Text(*document, "managedAssembly", true).empty()) throw std::runtime_error("Undeclared Managed deployment.");
        return result;
    } catch(const std::exception& exception) { return std::unexpected(Error(ErrorCategory::Framework, 245, exception.what())); }
}
/*-----------------------------------------------------------------------------------------
 * PackageStartup
 * - 配布manifestのinventoryを実行前に検証する。元マシンのEngine registryへ依存しない
 *---------------------------------------------------------------------------------------*/
struct PackageStartup {
    std::filesystem::path project; //< 配布Project。開発マシンのregistryを参照しない
    std::filesystem::path engineData; //< 共有Shader / Resourceの配布Root
    std::filesystem::path dotnetRoot; //< 空ならシステムRuntime、指定時は配布Root内の公式配置
};
/** \brief 配布ルート内の参照とhashを検証する。Game / Managedコードを起動しない */
inline Result<PackageStartup> LoadPackageStartup(const std::filesystem::path& executable, const std::string& engineId = {}, const std::string& engineVersion = {}) {
    using namespace ProjectJson;
    auto data = Read(executable.parent_path() / "NexusGame.package.json"); if(!data) return std::unexpected(data.error());
    try {
        Keys(*data, {"schemaVersion", "project", "engineData", "dotnetRoot", "files", "configuration", "engineId", "engineVersion"});
        if(data->at("schemaVersion") != 1 || !data->at("files").is_array() || data->at("files").empty() || data->at("files").size() > 100000) throw std::runtime_error("Invalid Package manifest.");
        if((!engineId.empty() && Text(*data, "engineId") != engineId) || (!engineVersion.empty() && Text(*data, "engineVersion") != engineVersion)) throw std::runtime_error("Package Engine identity mismatch.");
        const auto root = executable.parent_path().parent_path(); std::vector<std::filesystem::path> seen;
        const auto resolve = [&](const std::string& reference) -> Result<std::filesystem::path> {
            auto path = Path(reference); if(path.has_root_path()) return std::unexpected(Error(ErrorCategory::FileSystem, 245, "Absolute Package reference."));
            return ResolveProjectPath(root, path);
        };
        for(const auto& entry : data->at("files")) {
            Keys(entry, {"path", "sha256"}); const auto reference = Text(entry, "path");
            auto path = resolve(reference); if(!path) return std::unexpected(path.error());
            for(const auto& existing : seen) if(SameFilePath(existing, *path)) throw std::runtime_error("Duplicate Package inventory entry.");
            seen.push_back(*path); auto hash = Sha256File(*path);
            if(!hash || *hash != Text(entry, "sha256")) throw std::runtime_error("Package inventory mismatch: " + reference);
        }
        PackageStartup result; auto project = resolve(Text(*data, "project")), engine = resolve(Text(*data, "engineData"));
        if(!project) return std::unexpected(project.error()); if(!engine) return std::unexpected(engine.error());
        result.project = *project; result.engineData = *engine;
        for(const auto& required : {*project, executable, executable.parent_path() / "Nexus.modules.json"})
            if(std::ranges::none_of(seen, [&](const auto& file) { return SameFilePath(file, required); })) throw std::runtime_error("Package inventory omits a startup file.");
        if(!std::filesystem::is_directory(*engine / "Shaders")) throw std::runtime_error("Package Engine shaders missing.");
        const auto runtime = Text(*data, "dotnetRoot", true);
        if(!runtime.empty()) { auto path = resolve(runtime); if(!path) return std::unexpected(path.error()); result.dotnetRoot = *path; if(!std::filesystem::is_directory(*path / "host/fxr") || !std::filesystem::is_directory(*path / "shared/Microsoft.NETCore.App")) throw std::runtime_error("Invalid private .NET Runtime layout."); }
        return result;
    } catch(const std::exception& exception) { return std::unexpected(Error(ErrorCategory::FileSystem, 245, exception.what())); }
}
}
