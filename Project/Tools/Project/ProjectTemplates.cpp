#include "ProjectTemplates.h"
#include "Runtime/Project/ProjectJson.h"
#include "Runtime/Project/ProjectContext.h"
#include "Runtime/Application/ApplicationPaths.h"
#include "Runtime/Level/Level.h"
#include "Runtime/Scene/Components/CameraComponent.h"
#include "Platform/FileSystem/AtomicFile.h"
#include <fstream>
#include <set>
#include <algorithm>

namespace NexusEngine {
namespace {
using namespace ProjectJson;
Error Invalid(const std::string& message) { return Error(ErrorCategory::FileSystem, 191, message); }
void Replace(std::string& text, const std::string& token, const std::string& value) {
    size_t offset = 0;
    while((offset = text.find(token, offset)) != text.npos) { text.replace(offset, token.size(), value); offset += value.size(); }
}
std::string Xml(std::string value) {
    std::string literal;
    constexpr char hex[] = "0123456789ABCDEF";
    for(unsigned char ch : value) {
        if(std::string_view("%;$@()'*?").find(static_cast<char>(ch)) != std::string_view::npos) {
            literal += '%'; literal += hex[ch >> 4]; literal += hex[ch & 15];
        } else literal += static_cast<char>(ch);
    }
    value = std::move(literal);
    Replace(value, "&", "&amp;"); Replace(value, "<", "&lt;"); Replace(value, ">", "&gt;"); Replace(value, "\"", "&quot;"); Replace(value, "'", "&apos;"); return value;
}
Result<Json> Definition(const std::filesystem::path& file) {
    auto data = ProjectJson::Read(file); if(!data) return data;
    try {
        Keys(*data, {"schemaVersion", "id", "name", "description", "files", "moduleLanguages", "modules"});
        if(!data->at("schemaVersion").is_number_integer() || data->at("schemaVersion") != 1 || !data->at("files").is_array() || data->at("files").size() > 256)
            throw std::runtime_error("Invalid template schema.");
        for(const char* key : {"id", "name", "description"}) static_cast<void>(Text(*data, key));
        if(data->contains("modules")) {
            const auto& modules = data->at("modules");
            if(!modules.is_array() || modules.size() != 2) throw std::runtime_error("Hybrid template requires two Modules.");
            std::set<std::string> languages;
            for(const auto& item : modules) {
                Keys(item, {"id", "name", "language", "path", "type", "dependencies"});
                for(const char* key : {"id", "name", "path"}) static_cast<void>(Text(item, key));
                const auto language = Text(item, "language");
                if((language != "Cpp" && language != "CSharp") || !languages.insert(language).second || Text(item, "type") != "Game") throw std::runtime_error("Invalid Hybrid Module declarations.");
                if(item.contains("dependencies") && (!item.at("dependencies").is_array() || item.at("dependencies").size() > 256 ||
                    !std::ranges::all_of(item.at("dependencies"), [](const auto& dependency) { return dependency.is_string(); }))) throw std::runtime_error("Invalid template Module dependencies.");
            }
        }
        if(data->contains("moduleLanguages")) {
            const auto& languages = data->at("moduleLanguages");
            if(!languages.is_array() || languages.empty() || languages.size() > 2) throw std::runtime_error("Invalid template moduleLanguages.");
            std::set<std::string> unique;
            for(const auto& language : languages) {
                if(!language.is_string()) throw std::runtime_error("Invalid template language.");
                const auto value = language.get<std::string>();
                if((value != "Cpp" && value != "CSharp") || !unique.insert(value).second) throw std::runtime_error("Unsupported or duplicate template language.");
            }
        }
        return data;
    } catch(const std::exception& exception) { return std::unexpected(Invalid(exception.what())); }
}
}

Result<std::vector<ProjectTemplate>> ProjectTemplates::List(const std::filesystem::path& directory) {
    try {
        std::vector<ProjectTemplate> templates; std::set<std::string> ids;
        for(const auto& entry : std::filesystem::directory_iterator(directory)) {
            if(!entry.is_regular_file() || entry.path().extension() != ".nxtemplate") continue;
            auto data = Definition(entry.path()); if(!data) return std::unexpected(data.error());
            auto id = Text(*data, "id"); if(!ids.insert(id).second) return std::unexpected(Invalid("Duplicate template ID."));
            templates.push_back({id, Text(*data, "name"), Text(*data, "description"), std::filesystem::weakly_canonical(entry.path())});
        }
        std::ranges::sort(templates, {}, &ProjectTemplate::id);
        return templates;
    } catch(const std::exception& exception) { return std::unexpected(Invalid(exception.what())); }
}

////////////////////////////
// ファイル生成をstagingへ閉じ、失敗したテンプレートを公開しない
////////////////////////////
Result<ProjectContext> ProjectTemplates::Create(const std::filesystem::path& root, ProjectDescriptor descriptor,
    const ProjectTemplate& projectTemplate, const EngineInstallation& engine, ProjectModuleLanguage initialLanguage) {
    auto compatible = CheckProjectEngine(descriptor.engine, engine.identity); if(!compatible) return std::unexpected(compatible.error());
    auto definition = Definition(projectTemplate.file); if(!definition) return std::unexpected(definition.error());
    // Moduleは検証済みTemplate宣言から作る。UI由来の任意一覧を未検証で生成しない。
    if(!descriptor.modules.empty()) return std::unexpected(Invalid("Initial project generation requires an empty modules list."));
    if(initialLanguage != ProjectModuleLanguage::Cpp && initialLanguage != ProjectModuleLanguage::CSharp)
        return std::unexpected(Invalid("Unsupported initial Game language."));
    const std::string language = initialLanguage == ProjectModuleLanguage::Cpp ? "Cpp" : "CSharp";
    // 古いSDKのテンプレートは従来どおりCppのみ。未対応テンプレートのCppコードをC#へ混入させない。
    if(definition->contains("moduleLanguages")) {
        const auto& languages = definition->at("moduleLanguages");
        if(std::ranges::none_of(languages, [&](const auto& item) { return item == language; }))
            return std::unexpected(Invalid("Template does not support the selected initial Game language."));
    } else if(initialLanguage != ProjectModuleLanguage::Cpp) {
        return std::unexpected(Invalid("This Engine template supports Cpp only. Select an updated Engine for CSharp project generation."));
    }
    // 旧Installed SDKのホストはmodulesキーを認識しないため、旧テンプレートのCpp生成は
    // 従来形式を維持する。新テンプレートだけが明示的な初期Moduleを生成する。
    if(definition->contains("modules")) {
        const auto& modules = definition->at("modules");
        if(!modules.is_array() || modules.size() != 2) return std::unexpected(Invalid("Hybrid template requires one Native and one Managed Module."));
        std::set<std::string> languages;
        for(const auto& item : modules) {
            Keys(item, {"id", "name", "language", "path", "type", "dependencies"});
            const auto kind = Text(item, "language");
            if((kind != "Cpp" && kind != "CSharp") || !languages.insert(kind).second || Text(item, "type") != "Game") return std::unexpected(Invalid("Invalid Hybrid Module declarations."));
            auto path = Text(item, "path"); Replace(path, "@SOURCE@", descriptor.paths.source);
            descriptor.modules.push_back({Text(item, "id"), Text(item, "name"), kind == "Cpp" ? ProjectModuleLanguage::Cpp : ProjectModuleLanguage::CSharp, path, ProjectModuleType::Game});
            if(item.contains("dependencies")) for(const auto& dependency : item.at("dependencies"))
                descriptor.modules.back().dependencies.push_back(dependency.get<std::string>());
        }
        // 初期言語は先頭Moduleの生成順だけを決める。共通データや後続Moduleを制限しない。
        if(descriptor.modules.front().language != initialLanguage) std::swap(descriptor.modules[0], descriptor.modules[1]);
    } else if(definition->contains("moduleLanguages"))
        descriptor.modules.push_back({"Game", "Game", initialLanguage, descriptor.paths.source + "/Game", ProjectModuleType::Game});
    auto editorGuid = CreateUuid(); if(!editorGuid) return std::unexpected(editorGuid.error());
    auto moduleGuid = CreateUuid(); if(!moduleGuid) return std::unexpected(moduleGuid.error());
    auto nativeGuid = CreateUuid(); if(!nativeGuid) return std::unexpected(nativeGuid.error());
    return ProjectService::Create(root, std::move(descriptor), [&](const ProjectContext& context) -> Result<void> {
        try {
            std::set<std::filesystem::path> generated;
            const auto& project = context.Descriptor();
            for(const auto& item : definition->at("files")) {
                Keys(item, {"source", "target", "replace", "language"});
                // 言語指定のないLevel等は共通ファイルとして同じ生成・検証経路を通す。
                if(item.contains("language")) {
                    const auto fileLanguage = Text(item, "language");
                    if(fileLanguage != "Cpp" && fileLanguage != "CSharp") return std::unexpected(Invalid("Unsupported template file language."));
                    if(project.modules.empty() ? fileLanguage != language : std::ranges::none_of(project.modules, [&](const auto& module) { return (module.language == ProjectModuleLanguage::Cpp ? "Cpp" : "CSharp") == fileLanguage; })) continue;
                }
                std::string target = Text(item, "target");
                for(const auto& [token, value] : {std::pair{"@SOURCE@", project.paths.source}, {"@LEVELS@", project.paths.levels}, {"@CONFIG@", project.paths.config}}) Replace(target, token, value);
                const auto sourceReference = Path(Text(item, "source")); const auto targetReference = Path(target);
                if(sourceReference.has_root_path() || targetReference.has_root_path()) return std::unexpected(Invalid("Template paths must be relative."));
                auto source = ResolveProjectPath(projectTemplate.file.parent_path(), sourceReference);
                auto destination = ResolveProjectPath(context.Root(), targetReference);
                if(!source) return std::unexpected(source.error()); if(!destination) return std::unexpected(destination.error());
                if(!generated.insert(*destination).second || *destination == context.File() || destination->filename() == ".gitignore")
                    return std::unexpected(Invalid("Template would overwrite a protected/generated file."));
                if(std::filesystem::is_symlink(std::filesystem::symlink_status(*source)) || !std::filesystem::is_regular_file(*source))
                    return std::unexpected(Invalid("Template input must be a regular file inside the template root."));
                const auto size = std::filesystem::file_size(*source);
                if(size > 8 * 1024 * 1024) return std::unexpected(Invalid("Template file exceeds 8 MiB."));
                std::ifstream input(*source, std::ios::binary); std::string bytes{std::istreambuf_iterator<char>(input), {}};
                if(!input.eof() && input.fail()) return std::unexpected(Invalid("Cannot read template file."));
                if(!item.at("replace").is_boolean()) return std::unexpected(Invalid("Template replace must be boolean."));
                if(item.at("replace").get<bool>()) {
                    const bool xml = destination->extension() == ".vcxproj" || destination->extension() == ".slnx" || destination->extension() == ".csproj";
                    for(const auto& [token, value] : {std::pair{"@SOURCE@", project.paths.source}, {"@CONFIG@", project.paths.config},
                        {"@PROJECT_FILE@", Utf8(context.File().filename())}, {"@PROJECT_GUID@", project.projectId}, {"@EDITOR_GUID@", *editorGuid},
                        {"@MODULE_GUID@", *moduleGuid}, {"@ENGINE_ASSOCIATION@", project.engine.association}})
                        Replace(bytes, token, xml ? Xml(value) : value);
                    Replace(bytes, "@NATIVE_GUID@", xml ? Xml(*nativeGuid) : *nativeGuid);
                }
                std::error_code error; std::filesystem::create_directories(destination->parent_path(), error);
                if(error) return std::unexpected(Invalid("Cannot create template destination: " + error.message()));
                auto saved = WriteFileAtomically(*destination, bytes, false); if(!saved) return saved;
            }
            Level startup; auto loaded = startup.Load(context.StartupLevel()); if(!loaded) return loaded;
            bool camera = false;
            startup.GetScene()->ForEach<CameraComponent>([&](Entity, const CameraComponent& value) { camera |= value.primary && value.enabled; });
            if(!camera) return std::unexpected(Invalid("Runnable template requires an enabled primary Camera."));
            return {};
        } catch(const std::exception& exception) { return std::unexpected(Invalid(exception.what())); }
    });
}
}
