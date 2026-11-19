#include "GamePropertyMetadata.h"
namespace NexusEngine::GamePropertyMetadata {
ComponentEditorMetadata EditorMetadata(const Json& source, bool managed) {
    ComponentEditorMetadata result;
    result.category = "Gameplay";
    result.description = managed ? "componentBrowser.description.script" : "componentBrowser.description.game";
    result.icon = managed ? "component.Script" : "component.Default";
    result.origin = "Game";
    if(!source.contains("editor")) {
        return result;
    }
    const auto& object = source.at("editor");
    ProjectJson::Keys(object, {"displayName", "category", "description", "icon", "origin", "keywords"});
    const auto text = [&](const char* key, std::string& target, size_t limit) {
        if(!object.contains(key)) {
            return;
        }
        auto value = ProjectJson::Text(object, key, true);
        if(value.size() > limit) {
            throw std::runtime_error("Component editor metadata exceeds its text limit.");
        }
        if(!value.empty()) {
            target = std::move(value);
        }
    };
    text("displayName", result.displayName, 256);
    text("category", result.category, 64);
    text("description", result.description, 2048);
    text("icon", result.icon, 128);
    text("origin", result.origin, 64);
    if(object.contains("keywords")) {
        if(!object.at("keywords").is_array() || object.at("keywords").size() > 32) {
            throw std::runtime_error("Invalid Component search keywords.");
        }
        for(const auto& keyword : object.at("keywords")) {
            const auto value = keyword.get<std::string>();
            if(value.empty() || value.size() > 128 || value.find('\0') != value.npos) {
                throw std::runtime_error("Invalid Component search keyword.");
            }
            result.keywords.push_back(value);
        }
    }
    return result;
}
}
