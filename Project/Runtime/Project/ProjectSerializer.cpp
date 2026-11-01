#include "ProjectSerializer.h"
#include "ProjectJson.h"
#include <algorithm>
#include <cctype>

namespace NexusEngine {
namespace {
using namespace ProjectJson;
bool Identifier(const std::string& value, bool version = false) {
    return !value.empty() && value.size() <= 128 && std::ranges::all_of(value, [version](unsigned char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '_' || (version && ch == '+');
    });
}
void Relative(const std::string& value) {
    if(value.empty() || value.size() > 4096 || value.find('\\') != value.npos || value.find(':') != value.npos ||
       value.find_first_of("\r\n") != value.npos || value.find('\0') != value.npos) throw std::runtime_error("Invalid relative project path.");
    const auto path = Path(value);
    if(path.has_root_path()) throw std::runtime_error("Project paths must be relative.");
    for(const auto& part : path) {
        auto component = Utf8(part);
        if(component.empty()) continue;
        if(component == ".." || component == "." || component.back() == '.' || component.back() == ' ' ||
           component.find_first_of("<>\"|?*") != component.npos || std::ranges::any_of(component, [](unsigned char ch) { return ch < 32; }))
            throw std::runtime_error("Invalid Windows project path component.");
        auto base = component.substr(0, component.find('.'));
        std::ranges::transform(base, base.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
        if(base == "CON" || base == "PRN" || base == "AUX" || base == "NUL" ||
           (base.size() == 4 && (base.starts_with("COM") || base.starts_with("LPT")) && base[3] >= '1' && base[3] <= '9'))
            throw std::runtime_error("Windows device names cannot be project paths.");
    }
}
bool Extension(const std::filesystem::path& file) {
    auto extension = Utf8(file.extension());
    std::ranges::transform(extension, extension.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return extension == ".nxproject";
}
}

////////////////////////////
// 保存値だけを検証し、ファイルの実在性とは分離する
////////////////////////////
Result<void> ProjectSerializer::Validate(const ProjectDescriptor& descriptor) {
    try {
        if(descriptor.schemaVersion != 1) throw std::runtime_error("Unsupported project schemaVersion.");
        const auto& id = descriptor.projectId;
        if(id.size() != 36) throw std::runtime_error("projectId must be a canonical UUID.");
        bool nonzero = false;
        for(size_t index = 0; index < id.size(); ++index) {
            if(index == 8 || index == 13 || index == 18 || index == 23) { if(id[index] != '-') throw std::runtime_error("Invalid UUID separator."); }
            else {
                if(!((id[index] >= '0' && id[index] <= '9') || (id[index] >= 'a' && id[index] <= 'f'))) throw std::runtime_error("UUID must use lowercase hexadecimal digits.");
                nonzero |= id[index] != '0';
            }
        }
        if(!nonzero) throw std::runtime_error("Nil project UUID is not allowed.");
        if(descriptor.name.empty() || descriptor.name.size() > 256 ||
           std::ranges::any_of(descriptor.name, [](unsigned char ch) { return ch < 32; })) throw std::runtime_error("Invalid project name.");
        if(!Identifier(descriptor.projectVersion, true) || !Identifier(descriptor.engine.id)) throw std::runtime_error("Invalid projectVersion or engine.id.");
        if(descriptor.engine.association != "development" && descriptor.engine.association != "installed") throw std::runtime_error("Unknown Engine association.");
        if((descriptor.engine.association == "installed" || !descriptor.engine.version.empty()) && !Identifier(descriptor.engine.version, true)) throw std::runtime_error("A valid engine.version is required.");
        for(const auto* path : {&descriptor.paths.assets, &descriptor.paths.levels, &descriptor.paths.source, &descriptor.paths.config, &descriptor.startupLevel}) Relative(*path);
        // dumpはプログラムから渡された文字列のUTF-8も検証する。
        static_cast<void>(Json(descriptor.name).dump());
        for(const auto* path : {&descriptor.paths.assets, &descriptor.paths.levels, &descriptor.paths.source, &descriptor.paths.config, &descriptor.startupLevel}) static_cast<void>(Json(*path).dump());
        return {};
    } catch(const std::exception& exception) { return std::unexpected(Error(ErrorCategory::FileSystem, 161, exception.what())); }
}

////////////////////////////
// 未知の項目を黙って捨てず、対応スキーマだけを読む
////////////////////////////
Result<ProjectDescriptor> ProjectSerializer::Load(const std::filesystem::path& file) {
    if(!Extension(file)) return std::unexpected(Error(ErrorCategory::FileSystem, 161, "Expected a .nxproject file."));
    auto document = Read(file);
    if(!document) return std::unexpected(document.error());
    try {
        const auto& json = *document;
        Keys(json, {"schemaVersion", "projectId", "name", "projectVersion", "engine", "paths", "startupLevel"});
        if(!json.at("schemaVersion").is_number_integer() || json.at("schemaVersion") != 1) throw std::runtime_error("Unsupported project schemaVersion.");
        ProjectDescriptor descriptor;
        descriptor.projectId = Text(json, "projectId"); descriptor.name = Text(json, "name");
        descriptor.projectVersion = Text(json, "projectVersion"); descriptor.startupLevel = Text(json, "startupLevel");
        const auto& engine = json.at("engine"); Keys(engine, {"association", "id", "version"});
        descriptor.engine = {Text(engine, "association"), Text(engine, "id"), Text(engine, "version", true)};
        const auto& paths = json.at("paths"); Keys(paths, {"assets", "levels", "source", "config"});
        descriptor.paths = {Text(paths, "assets"), Text(paths, "levels"), Text(paths, "source"), Text(paths, "config")};
        auto valid = Validate(descriptor);
        if(!valid) return std::unexpected(valid.error());
        return descriptor;
    } catch(const std::exception& exception) { return std::unexpected(Error(ErrorCategory::FileSystem, 161, Utf8(file) + ": " + exception.what())); }
}

////////////////////////////
// Descriptorを検証してから原子的に公開する
////////////////////////////
Result<void> ProjectSerializer::Save(const std::filesystem::path& file, const ProjectDescriptor& descriptor, bool replace) {
    if(!Extension(file)) return std::unexpected(Error(ErrorCategory::FileSystem, 161, "Expected a .nxproject file."));
    auto valid = Validate(descriptor);
    if(!valid) return valid;
    const Json json{{"schemaVersion", descriptor.schemaVersion}, {"projectId", descriptor.projectId}, {"name", descriptor.name},
        {"projectVersion", descriptor.projectVersion}, {"startupLevel", descriptor.startupLevel},
        {"engine", {{"association", descriptor.engine.association}, {"id", descriptor.engine.id}, {"version", descriptor.engine.version}}},
        {"paths", {{"assets", descriptor.paths.assets}, {"levels", descriptor.paths.levels}, {"source", descriptor.paths.source}, {"config", descriptor.paths.config}}}};
    return Write(file, json, replace);
}
}
