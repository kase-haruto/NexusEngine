#include "NativeComponentMetadata.h"
#include "GamePropertyMetadata.h"
#include "Runtime/Scene/Components/GameComponentData.h"
#include "Public/Nexus/GameComponentMetadata.h"
#include <Windows.h>
#include <algorithm>
#include <cstring>
#include <set>
#include <stdexcept>

namespace NexusEngine {
namespace {
using namespace GamePropertyMetadata;
using ProjectJson::Json;
/** \brief 正規IDとAliasの保存scopeを探す。未知値を破棄せず、未来Versionの編集を拒否する */
std::optional<Json> Current(Entity entity, const TypeMetadata& type) {
    auto* data = entity.GetComponent<GameComponentData>(); if(!data) return {};
    auto root = State(data->propertyState); CompatibleState(root, type);
    for(const auto& name : type.names) if(root.at("types").contains(name)) return root;
    return {};
}
std::optional<PropertyValue> Get(Entity entity, const TypeMetadata& type, const PropertyMetadata& property) {
    try {
        const auto root = Current(entity, type); if(!root) return {};
        for(const auto& name : type.names) {
            if(!root->at("types").contains(name)) continue;
            const auto& values = root->at("types").at(name).at("values");
            for(const auto& key : property.names) if(values.contains(key) && values.at(key).contains(property.kind))
                return Value(property.kind, values.at(key).at(property.kind));
        }
        return property.initial;
    } catch(...) { return {}; }
}
/** \brief JSON候補を検証してから既存Storageの値だけを置換し、失敗時は元を保持する */
bool Set(Entity entity, const TypeMetadata& type, const PropertyMetadata& property, const PropertyValue& input) {
    try {
        auto root = Current(entity, type); if(!root || input.index() != property.initial.index()) return false;
        if(const auto* text = std::get_if<std::string>(&input); text && text->size() > 3072) return false;
        Json value; std::visit([&](const auto& item) { if constexpr(!std::is_same_v<std::decay_t<decltype(item)>, EnumValue>) value = item; }, input);
        static_cast<void>(Value(property.kind, value));
        auto& scope = (*root)["types"][type.id]; if(scope.is_null()) scope = Json{{"version", type.version}, {"values", Json::object()}};
        scope["version"] = type.version;
        auto& kinds = scope["values"][property.id]; if(kinds.is_null()) kinds = Json::object(); kinds[property.kind] = std::move(value);
        auto text = root->dump(); static_cast<void>(State(text));
        entity.GetComponent<GameComponentData>()->propertyState = std::move(text); return true;
    } catch(...) { return false; }
}
bool Add(Entity entity, const TypeMetadata& type) {
    try {
        if(Current(entity, type)) return true;
        auto* current = entity.GetComponent<GameComponentData>();
        GameComponentData next = current ? *current : GameComponentData{};
        auto root = State(next.propertyState); CompatibleState(root, type);
        Json values = Json::object();
        // 初回既定値も保存し、Moduleの既定値変更で既存Levelの意味を変えない。
        for(const auto& property : type.properties) {
            Json value; std::visit([&](const auto& item) { if constexpr(!std::is_same_v<std::decay_t<decltype(item)>, EnumValue>) value = item; }, property.initial);
            values[property.id] = Json{{property.kind, std::move(value)}};
        }
        root["types"][type.id] = Json{{"version", type.version}, {"values", std::move(values)}};
        next.propertyState = root.dump();
        const auto token = NextGameComponentAttachment(); if(!token) return false;
        if(!next.SetAttachment(type.id, token) || !next.IsValid()) return false;
        if(current) *current = std::move(next);
        else if(!entity.AddComponent<GameComponentData>(std::move(next))) return false;
        return true;
    } catch(...) { return false; }
}
bool Remove(Entity entity, const TypeMetadata& type) {
    try {
        auto root = Current(entity, type); if(!root) return false;
        auto* current = entity.GetComponent<GameComponentData>(); GameComponentData next = *current;
        for(const auto& name : type.names) { (*root)["types"].erase(name); next.RemoveAttachment(name); }
        // 最後の値Componentを外した時だけ、Engine格納Componentも既存ECSから削除する。
        if(root->at("types").empty()) return entity.RemoveComponent<GameComponentData>();
        next.propertyState = root->dump(); if(!next.IsValid()) return false;
        *current = std::move(next); return true;
    } catch(...) { return false; }
}
template<class Function> Function Export(HMODULE module, const char* name) {
    const auto address = GetProcAddress(module, name); Function function = nullptr;
    static_assert(sizeof(function) == sizeof(address)); std::memcpy(&function, &address, sizeof(function)); return function;
}
bool Match(NexusGameString text, const std::string& expected) { return text.data && text.length == expected.size() && std::string_view(text.data, text.length) == expected; }
/** \brief 契約違反の例外objectもDLL参照の生存中に破棄してからHostのエラーへ変換する */
template<class Function> NexusGameStatus CallExport(Function&& function) noexcept {
    try { return function(); } catch(...) { return NEXUS_GAME_FAILED; }
}
/* Inspection中もDllMain以外のゲーム初期化を呼ばない。値コピー後に必ず同じDLL参照を解放する。 */
struct ModuleReference {
    HMODULE handle; //< このinspectionだけが所有する参照。Runtime Loaderとは別
    ~ModuleReference() { if(handle) FreeLibrary(handle); }
    bool Close() { const auto value = handle; handle = nullptr; return !value || FreeLibrary(value) != 0; }
};
}
Result<std::shared_ptr<const NativeComponentMetadata>> NativeComponentMetadata::Parse(std::string_view text, const std::filesystem::path& sourceDirectory) {
    auto json = ProjectJson::Parse(text); if(!json) return std::unexpected(json.error());
    try {
        ProjectJson::Keys(*json, {"schemaVersion", "types"});
        if(!json->at("schemaVersion").is_number_integer() || json->at("schemaVersion") != 1 || !json->at("types").is_array() || json->at("types").size() > 256) throw std::runtime_error("Invalid Native Component metadata schema.");
        auto result = std::make_shared<NativeComponentMetadata>(); std::set<std::string> names;
        for(const auto& source : json->at("types")) {
            ProjectJson::Keys(source, {"id", "fullName", "version", "aliases", "properties", "sourceFiles"});
            auto type = std::make_shared<TypeMetadata>(); type->id = Id(ProjectJson::Text(source, "id")); type->label = Id(ProjectJson::Text(source, "fullName"));
            if(type->id.starts_with("ManagedScript:") || type->id == "GameComponents") throw std::runtime_error("Reserved Native Component ID.");
            if(!source.at("version").is_number_integer() || source.at("version") <= 0 || source.at("version") > INT32_MAX) throw std::runtime_error("Invalid Native Component version.");
            type->version = source.at("version").get<int>(); type->names = Names(source.at("aliases"), type->id);
            for(const auto& name : type->names) {
                if(name == "GameComponents" || name.starts_with("ManagedScript:")) throw std::runtime_error("Reserved Native Component alias.");
                if(!names.insert(name).second) throw std::runtime_error("Duplicate Native Component ID / alias.");
            }
            const auto& properties = source.at("properties"); if(!properties.is_array() || properties.size() > 256) throw std::runtime_error("Invalid Native properties.");
            std::set<std::string> propertyNames;
            for(const auto& field : properties) {
                ProjectJson::Keys(field, {"id", "label", "aliases", "kind", "default"});
                PropertyMetadata property; property.id = Id(ProjectJson::Text(field, "id")); property.label = Id(ProjectJson::Text(field, "label"));
                property.names = Names(field.at("aliases"), property.id);
                for(const auto& name : property.names) if(!propertyNames.insert(name).second) throw std::runtime_error("Duplicate Native Property ID / alias.");
                property.kind = ProjectJson::Text(field, "kind"); property.valueKind = Kind(property.kind); property.initial = Value(property.kind, field.at("default"));
                if(const auto* initialText = std::get_if<std::string>(&property.initial); initialText && initialText->size() > 3072) throw std::runtime_error("Native Property String exceeds 3072 bytes.");
                type->properties.push_back(std::move(property));
            }
            TypeDescriptor descriptor; descriptor.id = type->id; descriptor.labelKey = type->label; descriptor.valueBacked = true;
            if(source.contains("sourceFiles")) {
                const auto& files = source.at("sourceFiles"); if(!files.is_array() || files.size() > 64) throw std::runtime_error("Invalid Native source files.");
                for(const auto& file : files) {
                    const auto path = ProjectJson::Path(Id(file.get<std::string>()));
                    if(path.has_root_path()) throw std::runtime_error("Native source paths must be Module-relative.");
                    for(const auto& segment : path) if(segment == "..") throw std::runtime_error("Native source path escapes Module.");
                    if(!sourceDirectory.empty()) descriptor.sourceFiles.push_back(ProjectJson::Utf8((sourceDirectory / path).lexically_normal()));
                }
            }
            descriptor.contains = [type](Entity entity) {
                try {
                    const auto* data = entity.GetComponent<GameComponentData>(); if(!data) return false;
                    const auto root = State(data->propertyState);
                    return std::ranges::any_of(type->names, [&](const auto& name) { return root.at("types").contains(name); });
                } catch(...) { return false; }
            };
            descriptor.add = [type](Entity entity) { return Add(entity, *type); };
            descriptor.remove = [type](Entity entity) { return Remove(entity, *type); };
            for(const auto& property : type->properties) {
                descriptor.properties.push_back({property.id, property.label, property.valueKind,
                    [type, property](Entity entity) { return Get(entity, *type, property); },
                    [type, property](Entity entity, const PropertyValue& value) { return Set(entity, *type, property, value); }, {},
                    [type, property](Entity entity) -> std::string { try { static_cast<void>(Current(entity, *type)); return Get(entity, *type, property) ? "" : "Saved Native Property is incompatible with its metadata."; } catch(const std::exception& error) { return error.what(); } }});
            }
            result->types_.push_back(std::move(descriptor));
        }
        result->names_.assign(names.begin(), names.end());
        result->schema_ = json->dump();
        return std::shared_ptr<const NativeComponentMetadata>(std::move(result));
    } catch(const std::exception& error) { return std::unexpected(Error(ErrorCategory::Framework, 247, "Native Component metadata: " + std::string(error.what()))); }
}
Result<std::shared_ptr<const NativeComponentMetadata>> NativeComponentMetadata::Inspect(std::span<const NativeGameModuleSettings> modules) {
    try {
        if(modules.size() > 256) throw std::runtime_error("Too many Native Modules for metadata inspection.");
        auto combined = std::make_shared<NativeComponentMetadata>(); std::set<std::string> names;
        for(const auto& settings : modules) {
            if(!settings.file.is_absolute()) throw std::runtime_error("Native metadata requires an absolute Module path.");
            ModuleReference module{LoadLibraryExW(settings.file.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)};
            if(!module.handle) throw std::runtime_error("Cannot inspect Native Module: " + ProjectJson::Utf8(settings.file));
            const auto get = Export<NexusGetGameModuleFn>(module.handle, NEXUS_GAME_MODULE_ENTRY); NexusGameModuleV1 identity{};
            if(!get || CallExport([&] { return get(1, &identity); }) != NEXUS_GAME_OK || identity.abiVersion != 1 || identity.structSize != sizeof(identity) ||
                !identity.create || !identity.update || !identity.destroy ||
                !Match(identity.engineId, settings.engineId) || !Match(identity.engineVersion, settings.engineVersion) || !Match(identity.projectId, settings.projectId) || !Match(identity.moduleId, settings.moduleId))
                throw std::runtime_error("Native metadata Module identity mismatch.");
            const auto read = Export<NexusGetGameComponentMetadataFn>(module.handle, NEXUS_GAME_COMPONENT_METADATA_ENTRY);
            if(combined->moduleSchemas_.contains(settings.moduleId)) throw std::runtime_error("Duplicate Native metadata Module ID.");
            if(!read) {
                if(!module.Close()) throw std::runtime_error("Native metadata inspection could not release its DLL reference; restart the host.");
                combined->moduleSchemas_.emplace(settings.moduleId, ""); continue;
            } // 任意exportの省略は互換。
            NexusGameString text{};
            if(CallExport([&] { return read(1, &text); }) != NEXUS_GAME_OK || !text.data || !text.length || text.length > 1024 * 1024) throw std::runtime_error("Invalid Native metadata export.");
            // Parse中に全値をコピーする。CatalogへDLLの文字列・関数・object参照を残さない。
            auto parsed = Parse(std::string_view(text.data, text.length), settings.sourceDirectory); if(!parsed) return std::unexpected(parsed.error());
            if(!module.Close()) throw std::runtime_error("Native metadata inspection could not release its DLL reference; restart the host.");
            combined->moduleSchemas_.emplace(settings.moduleId, (*parsed)->schema_);
            for(const auto& name : (*parsed)->names_) if(!names.insert(name).second) throw std::runtime_error("Native Component ID / alias collides across Modules.");
            combined->types_.insert(combined->types_.end(), (*parsed)->types_.begin(), (*parsed)->types_.end());
            if(combined->types_.size() > 256) throw std::runtime_error("Too many Native Component types.");
        }
        combined->names_.assign(names.begin(), names.end());
        return std::shared_ptr<const NativeComponentMetadata>(std::move(combined));
    } catch(const std::exception& error) { return std::unexpected(Error(ErrorCategory::Framework, 247, error.what())); }
      catch(...) { return std::unexpected(Error(ErrorCategory::Framework, 247, "Native metadata export violated its exception contract.")); }
}
bool NativeComponentMetadata::Register(TypeRegistry& registry) const {
    for(const auto& name : names_) if(registry.Find(name)) return false;
    for(const auto& type : types_) if(!registry.Register(type)) return false;
    return true;
}
bool NativeComponentMetadata::Contains(std::string_view id) const noexcept {
    return std::ranges::any_of(types_, [&](const auto& type) { return type.id == id; });
}
std::string NativeComponentMetadata::ModuleSchema(std::string_view module) const {
    const auto found = moduleSchemas_.find(module);
    return found == moduleSchemas_.end() ? std::string{} : found->second;
}
}
