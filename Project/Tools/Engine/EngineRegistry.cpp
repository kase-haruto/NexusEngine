#include "EngineRegistry.h"
#include "Runtime/Project/ProjectJson.h"
#include "Runtime/Project/ProjectSerializer.h"
#include "Runtime/Application/ApplicationPaths.h"
#include "Platform/FileSystem/AtomicFile.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <regex>

namespace NexusEngine {
namespace {
using namespace ProjectJson;
Error Invalid(const std::string& message) { return Error(ErrorCategory::FileSystem, 190, message); }
bool Configuration(const std::string& value) { return value == "Debug" || value == "Develop" || value == "Release"; }
Result<std::filesystem::path> Within(const std::filesystem::path& root, const std::string& value) {
    const auto path = Path(value);
    if(path.empty() || path.has_root_path()) return std::unexpected(Invalid("Engine manifest paths must be relative."));
    for(const auto& part : path) if(part == "..") return std::unexpected(Invalid("Engine manifest path escapes root."));
    return ResolveProjectPath(root, path);
}
Result<Json> RegistryData(const std::filesystem::path& file) {
    std::error_code error;
    if(!std::filesystem::exists(file, error)) {
        if(error) return std::unexpected(Invalid("Cannot inspect Engine Registry: " + error.message()));
        return Json{{"schemaVersion", 1}, {"default", ""}, {"engines", Json::array()}};
    }
    auto data = ProjectJson::Read(file); if(!data) return data;
    try {
        Keys(*data, {"schemaVersion", "default", "engines"});
        if(!data->at("schemaVersion").is_number_integer() || data->at("schemaVersion") != 1 || !data->at("engines").is_array() || data->at("engines").size() > 64)
            throw std::runtime_error("Invalid Engine Registry schema.");
        static_cast<void>(Text(*data, "default", true));
        for(const auto& item : data->at("engines")) {
            Keys(item, {"manifest", "association", "id", "version", "configuration", "buildId"});
            for(const char* key : {"manifest", "association", "id", "version", "configuration", "buildId"}) static_cast<void>(Text(item, key));
            if(!Path(Text(item, "manifest")).is_absolute()) throw std::runtime_error("Engine Registry paths must be absolute.");
        }
        return data;
    } catch(const std::exception& exception) { return std::unexpected(Invalid(exception.what())); }
}
Result<void> SaveRegistry(const std::filesystem::path& file, const Json& data) {
    std::error_code error; std::filesystem::create_directories(file.parent_path(), error);
    if(error) return std::unexpected(Invalid("Cannot create Engine Registry directory: " + error.message()));
    return ProjectJson::Write(file, data);
}
}

////////////////////////////
// 配置と要求を別々に保持し、未配置版へ暗黙に差し替えない
////////////////////////////
Result<EngineInstallation> EngineManifest::Load(const std::filesystem::path& file) {
    auto document = ProjectJson::Read(file); if(!document) return std::unexpected(document.error());
    try {
        const auto& json = *document;
        Keys(json, {"schemaVersion", "association", "id", "version", "configuration", "buildId", "toolset", "runtimeLibrary", "paths", "runtimeFiles", "inventory"});
        if(!json.at("schemaVersion").is_number_integer() || json.at("schemaVersion") != 1) throw std::runtime_error("Unsupported Engine manifest schema.");
        EngineInstallation engine;
        engine.identity = {Text(json, "association"), Text(json, "id"), Text(json, "version")};
        ProjectDescriptor validation; validation.name = "Engine"; validation.projectId = "11111111-1111-4111-8111-111111111111"; validation.engine = engine.identity;
        auto valid = ProjectSerializer::Validate(validation); if(!valid) return std::unexpected(valid.error());
        engine.configuration = Text(json, "configuration"); engine.buildId = Text(json, "buildId");
        engine.toolset = Text(json, "toolset"); engine.runtimeLibrary = Text(json, "runtimeLibrary");
        if(!Configuration(engine.configuration) || engine.toolset != "v145" || engine.runtimeLibrary != (engine.configuration == "Release" ? "MT" : "MTd"))
            throw std::runtime_error("Unsupported Engine configuration, toolset or CRT.");
        engine.manifest = std::filesystem::weakly_canonical(std::filesystem::absolute(file)); engine.root = engine.manifest.parent_path();
        const auto& paths = json.at("paths"); Keys(paths, {"sdk", "data", "templates", "editor", "runtime"});
        for(const auto& [key, destination] : {std::pair{"sdk", &engine.sdk}, {"data", &engine.data}, {"templates", &engine.templates}, {"editor", &engine.editor}, {"runtime", &engine.runtime}}) {
            auto resolved = Within(engine.root, Text(paths, key)); if(!resolved) return std::unexpected(resolved.error()); *destination = *resolved;
        }
        for(const auto* directory : {&engine.sdk, &engine.data, &engine.templates})
            if(!std::filesystem::is_directory(*directory)) throw std::runtime_error("Missing Engine directory: " + Utf8(*directory));
        for(const auto* executable : {&engine.editor, &engine.runtime})
            if(!std::filesystem::is_regular_file(*executable)) throw std::runtime_error("Missing Engine host: " + Utf8(*executable));
        if(!std::filesystem::is_regular_file(engine.sdk / "Build/Nexus.Game.props") || !std::filesystem::is_directory(engine.data / "Shaders"))
            throw std::runtime_error("Engine SDK or Shader data is missing.");
        if(!json.at("runtimeFiles").is_array() || json.at("runtimeFiles").size() > 64) throw std::runtime_error("Invalid Engine runtime file list.");
        for(const auto& entry : json.at("runtimeFiles")) {
            if(!entry.is_string()) throw std::runtime_error("Runtime file path must be a string.");
            auto path = Within(engine.root, entry.get<std::string>()); if(!path) return std::unexpected(path.error());
            if(!std::filesystem::is_regular_file(*path)) throw std::runtime_error("Missing Engine runtime dependency: " + Utf8(*path));
            engine.runtimeFiles.push_back(*path);
        }
        const auto& inventory = json.at("inventory");
        if(!inventory.is_object()) throw std::runtime_error("Engine inventory must be an object.");
        if(engine.identity.association == "installed") {
            if(inventory.empty()) throw std::runtime_error("Installed Engine requires an immutable inventory.");
            for(const auto& [relative, digest] : inventory.items()) {
                auto path = Within(engine.root, relative); if(!path) return std::unexpected(path.error());
                auto hash = Sha256File(*path); if(!hash) return std::unexpected(hash.error());
                if(!digest.is_string() || digest.get<std::string>() != *hash) throw std::runtime_error("Installed Engine inventory mismatch: " + relative);
            }
            for(const auto* required : {&engine.editor, &engine.runtime, &engine.sdk}) {
                auto relative = Utf8(required->lexically_relative(engine.root));
                if(required == &engine.sdk) relative += "/Build/Nexus.Game.props";
                if(!inventory.contains(relative)) throw std::runtime_error("Installed Engine inventory lacks a required entry.");
            }
            for(const auto& dependency : engine.runtimeFiles) if(!inventory.contains(Utf8(dependency.lexically_relative(engine.root)))) throw std::runtime_error("Engine inventory lacks runtime dependency.");
            for(const auto* library : {"NexusRuntime.lib", "NexusEditorSupport.lib"}) {
                const auto filePath = engine.sdk / "Lib/x64" / engine.configuration / library;
                if(!std::filesystem::is_regular_file(filePath) || !inventory.contains(Utf8(filePath.lexically_relative(engine.root))))
                    throw std::runtime_error("Installed Engine SDK lacks a verified library.");
            }
            // 一覧に含まれないSDKヘッダ等の追加・混在も検出する。manifest自身は自己hashできない。
            for(const auto& entry : std::filesystem::recursive_directory_iterator(engine.root)) {
                if(entry.is_symlink()) throw std::runtime_error("Installed Engine must not contain symbolic links.");
                if(!entry.is_regular_file() || SameFilePath(entry.path(), engine.manifest)) continue;
                if(!inventory.contains(Utf8(entry.path().lexically_relative(engine.root))))
                    throw std::runtime_error("Installed Engine contains an untracked file: " + Utf8(entry.path()));
            }
        }
        return engine;
    } catch(const std::exception& exception) { return std::unexpected(Invalid(Utf8(file) + ": " + exception.what())); }
}

Result<std::filesystem::path> EngineManifest::DescribeDevelopment(const std::filesystem::path& repository,
    const std::string& configuration) {
    if(!Configuration(configuration)) return std::unexpected(Invalid("Select an explicit Development configuration."));
    const auto root = std::filesystem::weakly_canonical(std::filesystem::absolute(repository));
    // Launcher自身の版を別checkoutへ転記しない。選択したソースの既存ビルド定義を参照する。
    const auto identityFile = root / "Project/Build/Nexus.EngineIdentity.props";
    std::ifstream input(identityFile, std::ios::binary | std::ios::ate);
    if(!input || input.tellg() > 1024 * 1024) return std::unexpected(Invalid("Cannot read source Engine identity props."));
    input.seekg(0); const std::string bytes{std::istreambuf_iterator<char>(input), {}};
    const auto text = std::regex_replace(bytes, std::regex("<!--[\\s\\S]*?-->"), "");
    ProjectEngineRequirement identity;
    for(const auto& [property, output] : {std::pair{"NexusHostEngineId", &identity.id}, {"NexusHostEngineVersion", &identity.version}, {"NexusHostEngineAssociation", &identity.association}}) {
        const std::regex pattern(std::string("<") + property + "(?:\\s[^>]*)?>([^<]+)</" + property + ">");
        std::smatch match;
        if(!std::regex_search(text, match, pattern)) return std::unexpected(Invalid("Source Engine identity must be a literal property: " + std::string(property)));
        if(std::distance(std::sregex_iterator(text.begin(), text.end(), pattern), std::sregex_iterator{}) != 1)
            return std::unexpected(Invalid("Source Engine identity is ambiguous. Register an explicit manifest instead."));
        *output = match[1].str();
        if(output->find_first_of("$%&<>") != output->npos) return std::unexpected(Invalid("Source Engine identity expressions are not supported."));
    }
    ProjectDescriptor descriptor; descriptor.name = "Engine"; descriptor.projectId = "11111111-1111-4111-8111-111111111111"; descriptor.engine = identity;
    auto valid = ProjectSerializer::Validate(descriptor); if(!valid) return std::unexpected(valid.error());
    if(identity.association != "development") return std::unexpected(Invalid("Source Engine must declare Development association."));
    auto uuid = CreateUuid(); if(!uuid) return std::unexpected(uuid.error());
    const std::string binaries = "Generated/Modules/x64/" + configuration + "/";
    Json json{{"schemaVersion", 1}, {"association", identity.association}, {"id", identity.id}, {"version", identity.version},
        {"configuration", configuration}, {"buildId", *uuid}, {"toolset", "v145"}, {"runtimeLibrary", configuration == "Release" ? "MT" : "MTd"},
        {"paths", {{"sdk", "Project"}, {"data", "Project"}, {"templates", "Project/Templates"},
            {"editor", binaries + "NexusEditor/NexusEditor.exe"}, {"runtime", binaries + "NexusRuntimeHost/NexusRuntimeHost.exe"}}},
        {"runtimeFiles", {binaries + "NexusRuntimeHost/dxcompiler.dll", binaries + "NexusRuntimeHost/dxil.dll"}}, {"inventory", Json::object()}};
    const auto file = root / ("NexusEngine." + configuration + ".nxengine");
    // manifestの公開前に対象配置を確認する。失敗で既存の登録manifestを壊さない。
    for(const auto& relative : {"NexusEditor/NexusEditor.exe", "NexusRuntimeHost/NexusRuntimeHost.exe", "NexusRuntimeHost/dxcompiler.dll", "NexusRuntimeHost/dxil.dll"})
        if(!std::filesystem::is_regular_file(root / binaries / relative)) return std::unexpected(Invalid("Build the selected source Engine hosts before registration."));
    if(!std::filesystem::is_directory(root / "Project/Templates") || !std::filesystem::is_directory(root / "Project/Shaders"))
        return std::unexpected(Invalid("Source Engine templates or shaders are missing."));
    auto saved = ProjectJson::Write(file, json); if(!saved) return std::unexpected(saved.error());
    return file;
}

Result<std::filesystem::path> EngineRegistry::DefaultFile() {
    wchar_t* value = nullptr; size_t count = 0; _wdupenv_s(&value, &count, L"LOCALAPPDATA");
    std::filesystem::path root = value ? value : L""; std::free(value);
    if(!root.is_absolute()) return std::unexpected(Invalid("LOCALAPPDATA is unavailable."));
    return root / "NexusEngine/Engines/Registry.json";
}

Result<std::vector<RegisteredEngine>> EngineRegistry::Read() const {
    auto data = RegistryData(file_); if(!data) return std::unexpected(data.error());
    std::vector<RegisteredEngine> engines;
    for(const auto& item : data->at("engines")) {
        RegisteredEngine engine{{Text(item, "association"), Text(item, "id"), Text(item, "version")},
            Text(item, "configuration"), Text(item, "buildId"), {}, Path(Text(item, "manifest")), {}};
        auto loaded = EngineManifest::Load(engine.manifest);
        if(!loaded) engine.error = loaded.error().GetMessageText();
        else if(loaded->identity.id != engine.identity.id || loaded->identity.version != engine.identity.version ||
                loaded->identity.association != engine.identity.association || loaded->configuration != engine.configuration ||
                loaded->buildId != engine.buildId) engine.error = "Engine manifest changed. Explicitly register it again.";
        else engine.installation = std::move(*loaded);
        engines.push_back(std::move(engine));
    }
    return engines;
}

Result<void> EngineRegistry::Register(const std::filesystem::path& manifest, bool makeDefault) const {
    auto engine = EngineManifest::Load(manifest); if(!engine) return std::unexpected(engine.error());
    return WithFileLock(file_, [&]() -> Result<void> {
        auto data = RegistryData(file_); if(!data) return std::unexpected(data.error());
        auto& entries = data->at("engines");
        for(const auto& entry : entries) {
            if(Text(entry, "id") == engine->identity.id && Text(entry, "version") == engine->identity.version &&
               Text(entry, "association") == engine->identity.association && Text(entry, "configuration") == engine->configuration &&
               !SameFilePath(Path(Text(entry, "manifest")), engine->manifest)) return std::unexpected(Invalid("Engine identity/configuration is already registered at another location. Remove it explicitly first."));
        }
        std::erase_if(entries.get_ref<Json::array_t&>(), [&](const auto& entry) { return SameFilePath(Path(Text(entry, "manifest")), engine->manifest); });
        if(entries.size() >= 64) return std::unexpected(Invalid("Engine Registry is full."));
        entries.push_back({{"manifest", Utf8(engine->manifest)}, {"association", engine->identity.association}, {"id", engine->identity.id},
            {"version", engine->identity.version}, {"configuration", engine->configuration}, {"buildId", engine->buildId}});
        if(makeDefault) (*data)["default"] = Utf8(engine->manifest);
        return SaveRegistry(file_, *data);
    });
}
Result<void> EngineRegistry::Remove(const std::filesystem::path& manifest) const {
    const auto path = std::filesystem::weakly_canonical(std::filesystem::absolute(manifest));
    return WithFileLock(file_, [&]() -> Result<void> {
        auto data = RegistryData(file_); if(!data) return std::unexpected(data.error());
        auto& entries = data->at("engines").get_ref<Json::array_t&>();
        std::erase_if(entries, [&](const auto& entry) { return SameFilePath(Path(Text(entry, "manifest")), path); });
        if(SameFilePath(Path(Text(*data, "default", true)), path)) (*data)["default"] = "";
        return SaveRegistry(file_, *data);
    });
}
Result<void> EngineRegistry::SetDefault(const std::filesystem::path& manifest) const {
    auto engines = Read(); if(!engines) return std::unexpected(engines.error());
    const auto path = std::filesystem::weakly_canonical(std::filesystem::absolute(manifest));
    for(const auto& engine : *engines) if(SameFilePath(engine.manifest, path) && engine.installation) return Register(path, true);
    return std::unexpected(Invalid("Default Engine must be a valid registered installation."));
}
Result<std::optional<EngineInstallation>> EngineRegistry::Default() const {
    auto data = RegistryData(file_); if(!data) return std::unexpected(data.error());
    const auto path = Text(*data, "default", true); if(path.empty()) return std::optional<EngineInstallation>{};
    auto engines = Read(); if(!engines) return std::unexpected(engines.error());
    for(auto& engine : *engines) if(SameFilePath(engine.manifest, Path(path))) {
        if(!engine.installation) return std::unexpected(Invalid(engine.error));
        return std::optional<EngineInstallation>{std::move(*engine.installation)};
    }
    return std::unexpected(Invalid("Default Engine is no longer registered."));
}
Result<EngineInstallation> EngineRegistry::Resolve(const ProjectEngineRequirement& requested, const std::string& configuration) const {
    auto engines = Read(); if(!engines) return std::unexpected(engines.error());
    std::optional<EngineInstallation> match;
    for(const auto& engine : *engines) {
        if(engine.identity.id != requested.id || engine.identity.association != requested.association || engine.configuration != configuration ||
           (!requested.version.empty() && requested.version != engine.identity.version)) continue;
        if(!engine.installation) return std::unexpected(Invalid(engine.error));
        if(match) return std::unexpected(Invalid("Engine request is ambiguous. Select an exact version explicitly."));
        match = *engine.installation;
    }
    if(!match) return std::unexpected(Invalid("Required Engine is not registered for " + configuration + ": " + requested.id + " " + requested.version + ". No fallback was used."));
    return std::move(*match);
}
}
