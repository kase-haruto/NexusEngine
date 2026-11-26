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
/** \brief Templateの初期LevelをCPUで検証する。Camera等を補完・生成しない */
Result<void> ValidateStartup(const std::filesystem::path& file, bool requiresCamera) {
    Level startup;
    auto loaded = startup.Load(file);
    if(!loaded) {
        return loaded;
    }
    bool camera = false;
    startup.GetScene()->ForEach<CameraComponent>([&](Entity, const CameraComponent& value) {
        camera |= value.primary && value.enabled;
    });
    if(requiresCamera && !camera) {
        return std::unexpected(Invalid("Runnable template requires an enabled primary Camera."));
    }
    return {};
}
Result<Json> Definition(const std::filesystem::path& file) {
    auto data = ProjectJson::Read(file); if(!data) return data;
    try {
        Keys(*data, {"schemaVersion", "id", "name", "description", "files", "moduleLanguages", "modules", "category", "preview", "initialLevel", "requiresCamera", "generateModule"});
        if(!data->at("schemaVersion").is_number_integer() || data->at("schemaVersion") != 1 || !data->at("files").is_array() || data->at("files").size() > 256)
            throw std::runtime_error("Invalid template schema.");
        for(const char* key : {"id", "name", "description"}) static_cast<void>(Text(*data, key));
        // 旧SDKの定義はSample扱いを維持し、新しいBrowserへDemoを混入させない。
        for(const char* key : {"requiresCamera", "generateModule"}) {
            if(data->contains(key) && !data->at(key).is_boolean()) {
                throw std::runtime_error("Invalid template boolean.");
            }
        }
        if(data->contains("category")) {
            const auto category = Text(*data, "category");
            if(category != "project" && category != "sample") {
                throw std::runtime_error("Invalid template category.");
            }
        }
        if(data->contains("preview")) {
            Keys(data->at("preview"), {"kind", "source"});
            static_cast<void>(Text(data->at("preview"), "kind"));
            static_cast<void>(Text(data->at("preview"), "source"));
        }
        if(data->contains("initialLevel")) {
            static_cast<void>(Text(*data, "initialLevel"));
        }
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
        std::vector<ProjectTemplate> templates;
        for(const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
            if(!entry.is_regular_file() || entry.path().extension() != ".nxtemplate") {
                continue;
            }
            ProjectTemplate item;
            item.file = std::filesystem::weakly_canonical(entry.path());
            item.id = Utf8(entry.path().stem());
            item.name = item.id;
            item.category = "project";
            // 一件の不正Metadataで全Templateを失わない。作成時にも定義を再検証する。
            auto data = Definition(item.file);
            if(!data) {
                item.error = data.error().GetMessageText();
            } else {
                item.id = Text(*data, "id");
                item.name = Text(*data, "name");
                item.description = Text(*data, "description");
                item.category = data->value("category", "sample");
                item.initialLevel = data->value("initialLevel", "Levels/Startup.level");
                Replace(item.initialLevel, "@LEVELS@", "Levels");
                try {
                    auto level = ResolveProjectPath(directory, Path(item.initialLevel));
                    if(Path(item.initialLevel).has_root_path() || !level) {
                        throw std::runtime_error("Initial Level must be inside the Project.");
                    }
                    // Preview欠落は作成を妨げない。Source参照の不正もFallback表示へ閉じる。
                    if(data->contains("preview")) {
                        item.previewKind = Text(data->at("preview"), "kind");
                        const auto source = Path(Text(data->at("preview"), "source"));
                        auto preview = ResolveProjectPath(item.file.parent_path(), source);
                        if(!source.has_root_path() && preview) {
                            item.previewImage = *preview;
                        }
                    }
                    if(data->at("files").empty()) {
                        throw std::runtime_error("Template files are missing.");
                    }
                    bool startupDefined = false;
                    std::set<std::filesystem::path> targets;
                    for(const auto& file : data->at("files")) {
                        Keys(file, {"source", "target", "replace", "language"});
                        const auto source = Path(Text(file, "source"));
                        auto resolved = ResolveProjectPath(item.file.parent_path(), source);
                        if(source.has_root_path() || !resolved || !std::filesystem::is_regular_file(*resolved) ||
                            std::filesystem::is_symlink(std::filesystem::symlink_status(*resolved))) {
                            throw std::runtime_error("Template input is missing or outside the template root.");
                        }
                        std::string target = Text(file, "target");
                        Replace(target, "@SOURCE@", "Source");
                        Replace(target, "@LEVELS@", "Levels");
                        Replace(target, "@CONFIG@", "Config");
                        const auto reference = Path(target);
                        auto destination = ResolveProjectPath(directory, reference);
                        if(reference.has_root_path() || !destination || !targets.insert(*destination).second ||
                            !file.at("replace").is_boolean()) {
                            throw std::runtime_error("Invalid or duplicate template target.");
                        }
                        if(reference.lexically_normal() == Path(item.initialLevel).lexically_normal()) {
                            startupDefined = true;
                            // Source置換のないProject用Levelは列挙時にも検証し、Createを無効化できる。
                            if(item.category == "project" && !file.at("replace").get<bool>()) {
                                auto valid = ValidateStartup(*resolved, data->value("requiresCamera", false));
                                if(!valid) {
                                    throw std::runtime_error(valid.error().GetMessageText());
                                }
                            }
                        }
                    }
                    if(!startupDefined) {
                        throw std::runtime_error("Template does not provide its initial Level.");
                    }
                } catch(const std::exception& exception) {
                    item.error = exception.what();
                }
            }
            templates.push_back(std::move(item));
        }
        // 同じIDはどちらも無効にし、列挙順で別Templateを生成する事故を防ぐ。
        for(auto& item : templates) {
            if(std::ranges::count(templates, item.id, &ProjectTemplate::id) > 1) {
                item.error = "Duplicate template ID.";
            }
        }
        std::ranges::sort(templates, {}, &ProjectTemplate::id);
        return templates;
    } catch(const std::exception& exception) {
        return std::unexpected(Invalid(exception.what()));
    }
}

////////////////////////////
// ファイル生成をstagingへ閉じ、失敗したテンプレートを公開しない
////////////////////////////
Result<ProjectContext> ProjectTemplates::Create(const std::filesystem::path& root, ProjectDescriptor descriptor,
    const ProjectTemplate& projectTemplate, const EngineInstallation& engine, ProjectModuleLanguage initialLanguage) {
    if(!projectTemplate.error.empty()) {
        return std::unexpected(Invalid(projectTemplate.error));
    }
    auto compatible = CheckProjectEngine(descriptor.engine, engine.identity); if(!compatible) return std::unexpected(compatible.error());
    auto definition = Definition(projectTemplate.file); if(!definition) return std::unexpected(definition.error());
    if(definition->contains("initialLevel")) {
        descriptor.startupLevel = Text(*definition, "initialLevel");
        Replace(descriptor.startupLevel, "@LEVELS@", descriptor.paths.levels);
    }
    const bool generateModule = definition->value("generateModule", true);
    // Moduleは検証済みTemplate宣言から作る。UI由来の任意一覧を未検証で生成しない。
    if(!descriptor.modules.empty()) return std::unexpected(Invalid("Initial project generation requires an empty modules list."));
    if(initialLanguage != ProjectModuleLanguage::Cpp && initialLanguage != ProjectModuleLanguage::CSharp)
        return std::unexpected(Invalid("Unsupported initial Game language."));
    const std::string language = initialLanguage == ProjectModuleLanguage::Cpp ? "Cpp" : "CSharp";
    // 古いSDKのテンプレートは従来どおりCppのみ。未対応テンプレートのCppコードをC#へ混入させない。
    if(generateModule && definition->contains("moduleLanguages")) {
        const auto& languages = definition->at("moduleLanguages");
        if(std::ranges::none_of(languages, [&](const auto& item) { return item == language; }))
            return std::unexpected(Invalid("Template does not support the selected initial Game language."));
    } else if(generateModule && initialLanguage != ProjectModuleLanguage::Cpp) {
        return std::unexpected(Invalid("This Engine template supports Cpp only. Select an updated Engine for CSharp project generation."));
    }
    // 旧Installed SDKのホストはmodulesキーを認識しないため、旧テンプレートのCpp生成は
    // 従来形式を維持する。新テンプレートだけが明示的な初期Moduleを生成する。
    if(generateModule && definition->contains("modules")) {
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
    } else if(generateModule && definition->contains("moduleLanguages"))
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
            // Levelの要求条件はTemplateが所有する。UI名やIDでScene内容を分岐しない。
            auto valid = ValidateStartup(context.StartupLevel(), definition->value("requiresCamera", false));
            if(!valid) {
                return valid;
            }
            // 生成後にSceneが変更された場合は、共通画像の撮影対象にできない。
            // Preview用メタデータの書込み失敗でProject生成を中断しない。
            try {
                const auto hash = Sha256File(context.StartupLevel());
                const auto metadata = ResolveProjectPath(context.Root(), ".nexus-preview");
                std::error_code error;
                if(hash && metadata) {
                    std::filesystem::create_directories(*metadata, error);
                    if(!error) {
                        static_cast<void>(ProjectJson::Write(*metadata / "Template.json",
                            Json{{"id", projectTemplate.id}, {"initialLevelSha256", *hash}}));
                    }
                }
            } catch(...) {
                // Previewは任意のため、初期Levelの検証結果を維持する。
            }
            return {};
        } catch(const std::exception& exception) { return std::unexpected(Invalid(exception.what())); }
    });
}
}
