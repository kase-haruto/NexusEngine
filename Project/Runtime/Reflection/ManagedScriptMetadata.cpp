#include "ManagedScriptMetadata.h"
#include "GamePropertyMetadata.h"
#include "Runtime/Project/ProjectJson.h"
#include "Runtime/Scene/Components/ScriptComponent.h"
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <type_traits>
#include <limits>
#include <set>

namespace NexusEngine {
namespace {
using ProjectJson::Json;
using namespace GamePropertyMetadata;
/** \brief Entityから現在Componentを解決し、別Scriptの保存値を編集させない */
ScriptAttachment* Component(Entity entity, const TypeMetadata& type,PersistentId instance={}) {
    auto* component = entity.GetComponent<ScriptComponent>();
    if(instance.IsValid()) {
        auto* slot=component ? component->FindSlot(instance) : nullptr;
        return slot && std::ranges::find(type.names,slot->scriptType)!=type.names.end() ? slot : nullptr;
    }
    if(component) for(const auto& name : type.names) if(auto* slot = component->FindScript(name)) return slot;
    return nullptr;
}
/** \brief 保存値・旧ID・既定値の順に解決し、値コピーだけをEditorへ渡す */
std::optional<PropertyValue> Get(Entity entity, const TypeMetadata& type, const PropertyMetadata& property,PersistentId instance={}) {
    try {
        auto* component = Component(entity, type,instance); if(!component) return {};
        const auto root = State(component->propertyState);
        CompatibleState(root, type);
        const auto& types = root.at("types");
        // ID、旧IDの順に探索する。型の不一致は旧kindとして残し、現在の既定値を表示する。
        for(const auto& name : type.names) {
            if(!types.contains(name)) continue;
            const auto& values = types.at(name).at("values");
            for(const auto& key : property.names) if(values.contains(key) && values.at(key).contains(property.kind))
                return Value(property.kind, values.at(key).at(property.kind));
        }
        return property.initial;
    } catch(...) { return {}; }
}
/** \brief 状態候補を検証後に置換する。Editor所有threadで既存編集サービスから呼ぶ */
bool Set(Entity entity, const TypeMetadata& type, const PropertyMetadata& property, const PropertyValue& input,PersistentId instance={}) {
    try {
        auto* component = Component(entity, type,instance); if(!component || input.index() != property.initial.index()) return false;
        Json value;
        std::visit([&](const auto& item) {
            using T = std::decay_t<decltype(item)>;
            if constexpr(!std::is_same_v<T, EnumValue>) value = item;
        }, input);
        static_cast<void>(Value(property.kind, value));
        auto root = State(component->propertyState);
        CompatibleState(root, type);
        auto& scope = root["types"][type.id];
        if(scope.is_null()) scope = Json{{"version", type.version}, {"values", Json::object()}};
        scope["version"] = type.version;
        auto& kinds = scope["values"][property.id]; if(kinds.is_null()) kinds = Json::object();
        kinds[property.kind] = std::move(value);
        auto result = root.dump(); if(result.size() > 262144) return false;
        size_t total = result.size(); entity.GetComponent<ScriptComponent>()->ForEachScript([&](const auto& slot) { if(&slot != component) total += slot.propertyState.size(); });
        if(total > 1024 * 1024) return false;
        // 全検証が成功した後だけ置換し、Undo / 複数編集のrollbackを既存サービスへ任せる。
        component->propertyState = std::move(result); return true;
    } catch(...) { return false; }
}
}
bool ValidateScriptPropertyState(std::string_view text) noexcept { try { static_cast<void>(State(text)); return true; } catch(...) { return false; } }
Result<std::shared_ptr<const ManagedScriptMetadata>> ManagedScriptMetadata::Load(const std::filesystem::path& file) {
    auto json = ProjectJson::Read(file); if(!json) return std::unexpected(json.error());
    try {
        ProjectJson::Keys(*json, {"schemaVersion", "types"});
        if(!json->at("schemaVersion").is_number_integer() || json->at("schemaVersion") != 1 || !json->at("types").is_array() || json->at("types").size() > 256) throw std::runtime_error("Invalid metadata schema.");
        auto result = std::make_shared<ManagedScriptMetadata>();
        std::set<std::string> typeNames;
        for(const auto& source : json->at("types")) {
            ProjectJson::Keys(source, {"id", "fullName", "version", "aliases", "properties", "sourceFiles"});
            auto type = std::make_shared<TypeMetadata>();
            type->id = Id(ProjectJson::Text(source, "id")); type->label = Id(ProjectJson::Text(source, "fullName"));
            if(!source.at("version").is_number_integer() || source.at("version") <= 0 || source.at("version") > INT32_MAX) throw std::runtime_error("Invalid metadata version.");
            type->version = source.at("version").get<int>(); type->names = Names(source.at("aliases"), type->id);
            if(std::ranges::find(type->names, type->label) == type->names.end()) type->names.push_back(type->label);
            for(const auto& name : type->names) if(!typeNames.insert(name).second) throw std::runtime_error("Duplicate Script ID / alias.");
            const auto& properties = source.at("properties");
            if(!properties.is_array() || properties.size() > 256) throw std::runtime_error("Invalid properties.");
            std::set<std::string> propertyNames;
            for(const auto& field : properties) {
                ProjectJson::Keys(field, {"id", "label", "aliases", "kind", "default"});
                PropertyMetadata property;
                property.id = Id(ProjectJson::Text(field, "id")); property.label = Id(ProjectJson::Text(field, "label"));
                if(property.id == "@enabled") throw std::runtime_error("Reserved Script property ID.");
                property.names = Names(field.at("aliases"), property.id);
                for(const auto& name : property.names) if(!propertyNames.insert(name).second) throw std::runtime_error("Duplicate Property ID / alias.");
                property.kind = ProjectJson::Text(field, "kind"); property.valueKind = Kind(property.kind); property.initial = Value(property.kind, field.at("default"));
                type->properties.push_back(std::move(property));
            }
            TypeDescriptor descriptor;
            descriptor.id = "ManagedScript:" + type->id; descriptor.labelKey = type->label;
            // 旧sidecarは省略を許容する。パスは値Metadataとして扱い、使用時にEditorがRoot内を検査する。
            if(source.contains("sourceFiles")) {
                const auto& files = source.at("sourceFiles");
                if(!files.is_array() || files.size() > 64) throw std::runtime_error("Invalid Script source files.");
                for(const auto& sourceFile : files) {
                    auto path = sourceFile.get<std::string>();
                    if(path.empty() || path.size() > 32768 || path.find('\0') != path.npos)
                        throw std::runtime_error("Invalid Script source path.");
                    descriptor.sourceFiles.push_back(std::move(path));
                }
            }
            descriptor.contains = [type](Entity entity) { return Component(entity, *type) != nullptr; };
            descriptor.instances = [type](Entity entity) {
                std::vector<PersistentId> result;
                if(auto* owner=entity.GetComponent<ScriptComponent>()) owner->ForEachScript([&](const auto& slot) {
                    if(std::ranges::find(type->names,slot.scriptType)!=type->names.end()) result.push_back(slot.slotId);
                });
                return result;
            };
            descriptor.addInstance = [type](Entity entity) {
                auto* component = entity.GetComponent<ScriptComponent>();
                bool active = false; if(component) component->ForEachScript([&](const auto& slot) { active |= slot.instance.value != 0; });
                if(active) return false;
                if(!component) component = entity.AddComponent<ScriptComponent>();
                if(!component) return false;
                // 既存Scriptを置換せず、同じECS Componentに独立した保存値を追加する。
                if(component->scriptType.empty()) { component->scriptType = type->id; return true; }
                if(component->additional.size() >= 31) return false;
                ScriptAttachment slot; slot.scriptType = type->id; component->additional.push_back(std::move(slot)); return true;
            };
            descriptor.add = [type,add=descriptor.addInstance](Entity entity) { return Component(entity,*type) ? true : add(entity); };
            descriptor.remove = [type](Entity entity) {
                auto* owner = entity.GetComponent<ScriptComponent>(); auto* slot = Component(entity, *type);
                if(!owner || !slot || slot->instance.value) return false;
                if(owner->additional.empty()) return entity.RemoveComponent<ScriptComponent>();
                return owner->RemoveAttachment(slot);
            };
            descriptor.removeInstance = [type](Entity entity,PersistentId id) {
                auto* owner=entity.GetComponent<ScriptComponent>(); auto* slot=Component(entity,*type,id);
                if(!owner || !slot || slot->instance.value) return false;
                if(owner->additional.empty()) return entity.RemoveComponent<ScriptComponent>();
                return owner->RemoveAttachment(slot);
            };
            for(size_t index = 0; index < type->properties.size(); ++index) {
                const auto& property = type->properties[index];
                descriptor.properties.push_back({property.id, property.label, property.valueKind,
                    [type, index](Entity entity) { return Get(entity, *type, type->properties[index]); },
                    [type, index](Entity entity, const PropertyValue& value) { return Set(entity, *type, type->properties[index], value); }, {},
                    [type, index](Entity entity) -> std::string {
                        // 共通Detailsへ値だけの理由を渡す。取得失敗を既定値と誤認して編集させない。
                        try {
                            auto* component = Component(entity, *type); if(!component) return {};
                            CompatibleState(State(component->propertyState), *type);
                            if(!Get(entity, *type, type->properties[index])) return "Saved Property value is incompatible with its metadata.";
                            return {};
                        } catch(const std::exception& exception) { return exception.what(); }
                        catch(...) { return "Cannot read the saved Property value."; }
                    }});
                descriptor.properties.back().getInstance=[type,index](Entity entity,PersistentId id) { return Get(entity,*type,type->properties[index],id); };
                descriptor.properties.back().setInstance=[type,index](Entity entity,PersistentId id,const PropertyValue& value) { return Set(entity,*type,type->properties[index],value,id); };
            }
            descriptor.properties.push_back({"@enabled", "property.enabled", PropertyKind::Bool,
                [type](Entity entity) -> std::optional<PropertyValue> { if(auto* slot = Component(entity, *type)) return slot->enabled; return {}; },
                [type](Entity entity, const PropertyValue& input) { auto* slot = Component(entity, *type); const auto* enabled = std::get_if<bool>(&input); if(!slot || !enabled) return false; slot->enabled = *enabled; return true; }, {}});
            descriptor.properties.back().getInstance=[type](Entity entity,PersistentId id)->std::optional<PropertyValue> { if(auto* slot=Component(entity,*type,id)) return slot->enabled; return {}; };
            descriptor.properties.back().setInstance=[type](Entity entity,PersistentId id,const PropertyValue& input) {
                auto* slot=Component(entity,*type,id); const auto* enabled=std::get_if<bool>(&input);
                if(!slot || !enabled) return false; slot->enabled=*enabled; return true;
            };
            result->types_.push_back(std::move(descriptor));
        }
        return std::shared_ptr<const ManagedScriptMetadata>(std::move(result));
    } catch(const std::exception& exception) {
        return std::unexpected(Error(ErrorCategory::Framework, 234, "Managed metadata: " + std::string(exception.what())));
    }
}
bool ManagedScriptMetadata::Register(TypeRegistry& registry) const {
    for(const auto& type : types_) if(registry.Find(type.id)) return false;
    for(const auto& type : types_) if(!registry.Register(type)) return false;
    return true;
}
void ManagedScriptMetadata::Unregister(TypeRegistry& registry) const { for(const auto& type : types_) registry.Unregister(type.id); }
}
