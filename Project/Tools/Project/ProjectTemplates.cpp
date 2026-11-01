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
        Keys(*data, {"schemaVersion", "id", "name", "description", "files"});
        if(!data->at("schemaVersion").is_number_integer() || data->at("schemaVersion") != 1 || !data->at("files").is_array() || data->at("files").size() > 256)
            throw std::runtime_error("Invalid template schema.");
        for(const char* key : {"id", "name", "description"}) static_cast<void>(Text(*data, key));
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
    const ProjectTemplate& projectTemplate, const EngineInstallation& engine) {
    auto compatible = CheckProjectEngine(descriptor.engine, engine.identity); if(!compatible) return std::unexpected(compatible.error());
    auto definition = Definition(projectTemplate.file); if(!definition) return std::unexpected(definition.error());
    auto editorGuid = CreateUuid(); if(!editorGuid) return std::unexpected(editorGuid.error());
    return ProjectService::Create(root, std::move(descriptor), [&](const ProjectContext& context) -> Result<void> {
        try {
            std::set<std::filesystem::path> generated;
            const auto& project = context.Descriptor();
            for(const auto& item : definition->at("files")) {
                Keys(item, {"source", "target", "replace"});
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
                    const bool xml = destination->extension() == ".vcxproj" || destination->extension() == ".slnx";
                    for(const auto& [token, value] : {std::pair{"@SOURCE@", project.paths.source}, {"@CONFIG@", project.paths.config},
                        {"@PROJECT_FILE@", Utf8(context.File().filename())}, {"@PROJECT_GUID@", project.projectId}, {"@EDITOR_GUID@", *editorGuid}, {"@ENGINE_ASSOCIATION@", project.engine.association}})
                        Replace(bytes, token, xml ? Xml(value) : value);
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
