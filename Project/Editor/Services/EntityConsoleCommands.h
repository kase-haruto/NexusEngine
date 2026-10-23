#pragma once
#include "ConsoleService.h"
#include "SceneEditService.h"
#include <charconv>
#include <iomanip>
#include <sstream>
#include <type_traits>

namespace NexusEngine {
// Holds service references only; every command resolves entities in the current document.
class EntityConsoleCommands final {
public:
    EntityConsoleCommands(EditorSession& session, PropertyEditService& properties, SceneEditService& edits,
        const TypeRegistry& types, std::function<bool()> canEdit)
        : session_(session), properties_(properties), edits_(edits), types_(types), canEdit_(std::move(canEdit)) {}

    void Register(ConsoleService& console) const {
        for(const std::string action : {"list", "create", "select", "info", "delete", "rename", "set", "parent", "add"})
            console.Register("entity." + action, [self = *this, action](std::string_view args) { return self.Execute(action, args); });
    }
private:
    static std::string IdText(PersistentId id) {
        std::ostringstream out;
        out << '#' << std::hex << std::setfill('0') << std::setw(16) << id.high << std::setw(16) << id.low;
        return out.str();
    }
    static bool Tokens(std::string_view args, std::vector<std::string>& tokens) {
        std::istringstream input{std::string(args)};
        while(input >> std::ws && !input.eof()) {
            std::string token;
            if(!(input >> std::quoted(token))) return false;
            // A closing quote must be followed by whitespace or the end of input.
            const auto next = input.peek();
            if(next != EOF && next != ' ' && next != '\t' && next != '\r' && next != '\n') return false;
            tokens.push_back(std::move(token));
        }
        return true;
    }
    Entity Resolve(std::string_view target, std::string& error) const {
        auto* level = session_.GetLevel();
        if(!level || !level->GetScene()) { error = "No level is open."; return {}; }
        Entity result;
        if(target == "@selected") result = level->FindEntity(session_.GetSelection());
        else if(target.starts_with('#')) {
            PersistentId id;
            auto parse = [](std::string_view text, uint64_t& value) {
                auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
                return ec == std::errc{} && end == text.data() + text.size();
            };
            if(target.size() != 33 || !parse(target.substr(1,16), id.high) || !parse(target.substr(17), id.low)) {
                error = "Invalid entity ID; copy the #ID from entity.list."; return {};
            }
            result = level->FindEntity(id);
        } else {
            size_t matches = 0;
            level->GetScene()->ForEach<NameComponent>([&](Entity entity, const NameComponent& name) {
                if(name.name == target) { result = entity; ++matches; }
            });
            if(matches > 1) { error = "Ambiguous entity name; use the #ID from entity.list."; return {}; }
        }
        if(!result.IsAlive()) error = "Entity not found.";
        return result;
    }
    static std::string ValueText(const PropertyValue& value) {
        return std::visit([](const auto& item) {
            using T = std::decay_t<decltype(item)>;
            std::ostringstream out;
            out << std::setprecision(9);
            if constexpr(std::is_same_v<T, std::string>) out << std::quoted(item);
            else if constexpr(std::is_same_v<T, bool>) out << (item ? "true" : "false");
            else if constexpr(std::is_same_v<T, EnumValue>) out << item.value;
            else if constexpr(std::is_arithmetic_v<T>) out << item;
            else { for(size_t i = 0; i < item.size(); ++i) { if(i) out << ' '; out << item[i]; } }
            return out.str();
        }, value);
    }
    static bool ParseValue(const std::vector<std::string>& tokens, PropertyValue& value) {
        return std::visit([&](auto& item) {
            using T = std::decay_t<decltype(item)>;
            if constexpr(std::is_same_v<T, std::string>) {
                if(tokens.size() != 1) return false;
                item = tokens[0]; return true;
            } else if constexpr(std::is_same_v<T, bool>) {
                if(tokens.size() != 1 || (tokens[0] != "true" && tokens[0] != "false")) return false;
                item = tokens[0] == "true"; return true;
            } else {
                auto number = [](const std::string& text, auto& result) {
                    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), result);
                    return ec == std::errc{} && end == text.data() + text.size();
                };
                if constexpr(std::is_same_v<T, EnumValue>) return tokens.size() == 1 && number(tokens[0], item.value);
                else if constexpr(std::is_arithmetic_v<T>) return tokens.size() == 1 && number(tokens[0], item);
                else {
                    if(tokens.size() != item.size()) return false;
                    for(size_t i = 0; i < item.size(); ++i) if(!number(tokens[i], item[i])) return false;
                    return true;
                }
            }
        }, value);
    }
    ConsoleService::Output Execute(const std::string& action, std::string_view args) const {
        std::vector<std::string> tokens;
        if(!Tokens(args, tokens)) return "Invalid arguments: use double quotes around names containing spaces.";
        const auto usage = [&]() -> ConsoleService::Output {
            if(action == "list") return "Usage: entity.list [name-filter]";
            if(action == "create") return "Usage: entity.create <Entity|Model|Camera|DirectionalLight> [name]";
            if(action == "rename") return "Usage: entity.rename <target> <name>";
            if(action == "set") return "Usage: entity.set <target> <Component.property> <value...>";
            if(action == "parent") return "Usage: entity.parent <target> <parent|@root>";
            if(action == "add") return "Usage: entity.add <target> <Component>";
            return "Usage: entity." + action + " <name|#ID|@selected>";
        };
        auto* level = session_.GetLevel();
        if(!level || !level->GetScene()) return "No level is open.";
        if(action == "list") {
            if(tokens.size() > 1) return usage();
            std::string result;
            level->GetScene()->ForEach<PersistentIdComponent, NameComponent>([&](Entity, const auto& id, const auto& name) {
                if(tokens.empty() || name.name.find(tokens[0]) != std::string::npos)
                    result += IdText(id.value) + "  " + ValueText(name.name) + (session_.Selection().Contains(id.value) ? " *\n" : "\n");
            });
            return result.empty() ? "No matching entities." : result;
        }
        if(action == "create") {
            if(tokens.empty() || tokens.size() > 2 || (tokens.size() == 2 && tokens[1].empty())) return usage();
            if(!canEdit_()) return {"console.unavailable", true};
            if(!edits_.Create(tokens[0], tokens.size() == 2 ? tokens[1] : std::string{})) return "Entity creation rejected; check the kind and name.";
            return "Created " + IdText(session_.GetSelection());
        }
        const size_t count = action == "set" ? 3 : (action == "rename" || action == "parent" || action == "add" ? 2 : 1);
        if(tokens.size() < count || (action != "set" && tokens.size() != count)) return usage();
        std::string error;
        auto entity = Resolve(tokens[0], error);
        if(!entity.IsAlive()) return error;
        const auto* persistent = entity.GetComponent<PersistentIdComponent>();
        if(!persistent) return "Entity has no persistent ID.";
        const auto id = persistent->value;
        if(action == "info") {
            std::string result = IdText(id) + '\n';
            for(const auto& [typeId, type] : types_.Types()) if(type.contains(entity)) {
                result += '[' + typeId + "]\n";
                for(const auto& property : type.properties)
                    if(auto value = property.get(entity)) result += "  " + typeId + '.' + property.id + " = " + ValueText(*value) + '\n';
            }
            return result;
        }
        if(!canEdit_()) return {"console.unavailable", true};
        if(action == "select") { properties_.Commit(); session_.Select(id); return "Selected " + IdText(id); }
        bool changed = false;
        if(action == "rename" || action == "set") {
            PropertyHandle handle{id, "Name", "name"};
            if(action == "set") {
                const auto dot = tokens[1].find('.');
                if(dot == std::string::npos) return usage();
                handle.type = tokens[1].substr(0,dot); handle.property = tokens[1].substr(dot+1);
            }
            auto value = properties_.Read(handle);
            if(!value) return "Component or property not found; use entity.info <target>.";
            const std::vector<std::string> values(tokens.begin() + (action == "set" ? 2 : 1), tokens.end());
            if(!ParseValue(values, *value)) return "Invalid value type or number of values; use entity.info <target>.";
            properties_.Commit(); changed = properties_.Preview(handle, *value); properties_.Commit();
        } else if(action == "parent") {
            PersistentId parent;
            if(tokens[1] != "@root") {
                auto target = Resolve(tokens[1], error);
                if(!target.IsAlive()) return error;
                const auto* parentId = target.GetComponent<PersistentIdComponent>();
                if(!parentId) return "Parent entity has no persistent ID.";
                parent = parentId->value;
            }
            changed = edits_.Reparent(id, parent);
        } else {
            const auto* type = action == "add" ? types_.Find(tokens[1]) : nullptr;
            if(action == "add" && (!type || !type->add)) return "Component cannot be added.";
            const std::vector<PersistentId> previous(session_.Selection().All().begin(), session_.Selection().All().end());
            properties_.Commit(); session_.Select(id);
            changed = action == "delete" ? edits_.DeleteSelection() : edits_.AddComponent(*type);
            if(!changed) session_.Selection().Replace(previous);
        }
        return changed ? "OK" : "Edit rejected; check the value and entity state.";
    }
    EditorSession& session_;
    PropertyEditService& properties_;
    SceneEditService& edits_;
    const TypeRegistry& types_;
    std::function<bool()> canEdit_;
};
}
