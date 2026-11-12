#include "ProjectSerializer.h"
#include "ProjectJson.h"
#include <algorithm>
#include <cctype>
#include <set>
#include <map>

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
/** \brief 宣言順で深さ優先探索する。新しいModule Managerを作らず、保存値の整合性だけを扱う */
std::vector<size_t> OrderedModules(const ProjectDescriptor& descriptor) {
    std::map<std::string, size_t> indices;
    for(size_t index = 0; index < descriptor.modules.size(); ++index) indices.emplace(descriptor.modules[index].id, index);
    std::vector<uint8_t> states(descriptor.modules.size(), 0); std::vector<size_t> order;
    for(const auto& module : descriptor.modules) {
        if(module.dependencies.size() > descriptor.modules.size()) throw std::runtime_error("Too many dependencies: " + module.id);
        std::set<std::string> unique;
        for(const auto& dependency : module.dependencies) {
            if(!Identifier(dependency) || !indices.contains(dependency)) throw std::runtime_error("Unknown Module dependency: " + module.id + " -> " + dependency);
            if(dependency == module.id || !unique.insert(dependency).second) throw std::runtime_error("Self or duplicate Module dependency: " + module.id + " -> " + dependency);
        }
    }
    const auto visit = [&](auto&& self, size_t index) -> void {
        if(states[index] == 2) return;
        if(states[index] == 1) throw std::runtime_error("Cyclic Module dependency at: " + descriptor.modules[index].id);
        states[index] = 1;
        for(const auto& dependency : descriptor.modules[index].dependencies) self(self, indices.at(dependency));
        states[index] = 2; order.push_back(index);
    };
    for(size_t index = 0; index < descriptor.modules.size(); ++index) visit(visit, index);
    return order;
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
        // Module宣言は値として検証する。Sourceの存在やロード可否をここで要求しない。
        if(descriptor.modules.size() > 256) throw std::runtime_error("Project exceeds 256 modules.");
        std::set<std::string> moduleIds;
        for(const auto& module : descriptor.modules) {
            if(!Identifier(module.id) || !moduleIds.insert(module.id).second) throw std::runtime_error("Invalid or duplicate Module ID.");
            if(module.name.empty() || module.name.size() > 256 ||
               std::ranges::any_of(module.name, [](unsigned char ch) { return ch < 32; })) throw std::runtime_error("Invalid Module name.");
            if(module.language != ProjectModuleLanguage::Cpp && module.language != ProjectModuleLanguage::CSharp)
                throw std::runtime_error("Unsupported Module language.");
            if(module.type != ProjectModuleType::Game) throw std::runtime_error("Unsupported Module type.");
            Relative(module.path);
            static_cast<void>(Json(module.name).dump());
            static_cast<void>(Json(module.path).dump());
        }
        static_cast<void>(OrderedModules(descriptor));
        return {};
    } catch(const std::exception& exception) { return std::unexpected(Error(ErrorCategory::FileSystem, 161, exception.what())); }
}
Result<std::vector<size_t>> ProjectSerializer::DependencyOrder(const ProjectDescriptor& descriptor) {
    auto valid = Validate(descriptor); if(!valid) return std::unexpected(valid.error());
    try { return OrderedModules(descriptor); }
    catch(const std::exception& exception) { return std::unexpected(Error(ErrorCategory::FileSystem, 161, exception.what())); }
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
        Keys(json, {"schemaVersion", "projectId", "name", "projectVersion", "engine", "paths", "startupLevel", "modules"});
        if(!json.at("schemaVersion").is_number_integer() || json.at("schemaVersion") != 1) throw std::runtime_error("Unsupported project schemaVersion.");
        ProjectDescriptor descriptor;
        descriptor.projectId = Text(json, "projectId"); descriptor.name = Text(json, "name");
        descriptor.projectVersion = Text(json, "projectVersion"); descriptor.startupLevel = Text(json, "startupLevel");
        const auto& engine = json.at("engine"); Keys(engine, {"association", "id", "version"});
        descriptor.engine = {Text(engine, "association"), Text(engine, "id"), Text(engine, "version", true)};
        const auto& paths = json.at("paths"); Keys(paths, {"assets", "levels", "source", "config"});
        descriptor.paths = {Text(paths, "assets"), Text(paths, "levels"), Text(paths, "source"), Text(paths, "config")};
        // 既存schema 1はmodules省略のまま受け入れ、推測したModuleを保存しない。
        if(json.contains("modules")) {
            const auto& modules = json.at("modules");
            if(!modules.is_array() || modules.size() > 256) throw std::runtime_error("Invalid modules array.");
            for(const auto& item : modules) {
                Keys(item, {"id", "name", "language", "path", "type", "dependencies"});
                const auto language = Text(item, "language");
                if(language != "Cpp" && language != "CSharp") throw std::runtime_error("Unsupported Module language.");
                if(Text(item, "type") != "Game") throw std::runtime_error("Unsupported Module type.");
                descriptor.modules.push_back({Text(item, "id"), Text(item, "name"),
                    language == "Cpp" ? ProjectModuleLanguage::Cpp : ProjectModuleLanguage::CSharp,
                    Text(item, "path"), ProjectModuleType::Game});
                if(item.contains("dependencies")) {
                    const auto& dependencies = item.at("dependencies");
                    if(!dependencies.is_array() || dependencies.size() > 256) throw std::runtime_error("Invalid Module dependencies.");
                    for(const auto& dependency : dependencies) {
                        if(!dependency.is_string()) throw std::runtime_error("Module dependency must be an ID string.");
                        descriptor.modules.back().dependencies.push_back(dependency.get<std::string>());
                    }
                }
            }
        }
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
    Json json{{"schemaVersion", descriptor.schemaVersion}, {"projectId", descriptor.projectId}, {"name", descriptor.name},
        {"projectVersion", descriptor.projectVersion}, {"startupLevel", descriptor.startupLevel},
        {"engine", {{"association", descriptor.engine.association}, {"id", descriptor.engine.id}, {"version", descriptor.engine.version}}},
        {"paths", {{"assets", descriptor.paths.assets}, {"levels", descriptor.paths.levels}, {"source", descriptor.paths.source}, {"config", descriptor.paths.config}}}};
    // 従来のデータProjectは従来形式のまま保存し、Module追加時だけ拡張項目を出力する。
    if(!descriptor.modules.empty()) {
        json["modules"] = Json::array();
        for(const auto& module : descriptor.modules) {
            json["modules"].push_back({{"id", module.id}, {"name", module.name},
                {"language", module.language == ProjectModuleLanguage::Cpp ? "Cpp" : "CSharp"},
                {"path", module.path}, {"type", "Game"}});
            // 依存なしの既存Projectでは保存形式を変えず、必要な宣言だけを拡張する。
            if(!module.dependencies.empty()) json["modules"].back()["dependencies"] = module.dependencies;
        }
    }
    return Write(file, json, replace);
}
}
