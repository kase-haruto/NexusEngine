#include "ComponentBrowserModel.h"
#include "Editor/Gui/Panels/IEditorPanel.h"
#include "Editor/Core/EditorSession.h"
#include "Editor/Services/LocalizationService.h"
#include <algorithm>
#include <cctype>
#include <map>

namespace NexusEngine {
namespace {
std::string Fold(std::string_view value) {
    std::string result(value);
    // UTF-8の多byte文字は維持し、英字だけ大文字小文字を区別しない。
    for(char& character : result) {
        const auto byte = static_cast<unsigned char>(character);
        if(byte < 128) {
            character = static_cast<char>(std::tolower(byte));
        }
    }
    return result;
}
std::string HintLabel(const LocalizationService& text, const char* prefix, const std::string& id) {
    const auto key = std::string(prefix) + id;
    const auto value = text.Text(key);
    return value == key ? id : value;
}
}
void ComponentBrowserModel::Update(const TypeRegistry& registry, const LocalizationService& text) {
    if(registry_ == &registry && text_ == &text && generation_ == registry.Generation() && language_ == text.Language()) {
        return;
    }
    registry_ = &registry;
    text_ = &text;
    generation_ = registry.Generation();
    language_ = text.Language();
    entries_.clear();
    categories_.clear();
    std::map<std::string, std::string> categories;
    for(const auto& [id, type] : registry.Types()) {
        if(!type.contains || type.labelKey.empty()) {
            continue;
        }
        const auto& metadata = type.editor;
        ComponentBrowserEntry entry;
        entry.id = id;
        entry.name = text.Text(metadata.displayName.empty() ? type.labelKey : metadata.displayName);
        entry.category = metadata.category.empty() ? "Other" : metadata.category;
        entry.categoryName = HintLabel(text, "componentBrowser.category.", entry.category);
        entry.description = text.Text(metadata.description.empty() ? "componentBrowser.description.default" : metadata.description);
        entry.icon = metadata.icon.empty() ? "component.Default" : metadata.icon;
        entry.origin = metadata.origin.empty() ? "Custom" : metadata.origin;
        entry.originName = HintLabel(text, "componentBrowser.origin.", entry.origin);
        std::string search = entry.name + " " + id + " " + entry.description + " " + entry.category + " " + entry.categoryName;
        search += " " + type.labelKey + " " + metadata.description;
        for(const auto& keyword : metadata.keywords) {
            search += " " + keyword;
        }
        entry.searchText = Fold(search);
        categories[entry.category] = entry.categoryName;
        entries_.push_back(std::move(entry));
    }
    std::ranges::sort(entries_, [](const auto& left, const auto& right) {
        return left.name < right.name;
    });
    for(const auto& category : categories) {
        categories_.push_back(category);
    }
}
const std::vector<ComponentBrowserEntry>& ComponentBrowserModel::Entries() const noexcept {
    return entries_;
}
const std::vector<std::pair<std::string, std::string>>& ComponentBrowserModel::Categories() const noexcept {
    return categories_;
}
std::vector<size_t> ComponentBrowserModel::Filter(std::string_view query, std::string_view category,
    std::span<const std::string> favorites, bool favoritesOnly) const {
    const auto folded = Fold(query);
    std::vector<size_t> result;
    for(size_t index = 0; index < entries_.size(); ++index) {
        const auto& entry = entries_[index];
        if((!category.empty() && entry.category != category) ||
            (favoritesOnly && std::ranges::find(favorites, entry.id) == favorites.end()) ||
            (!folded.empty() && entry.searchText.find(folded) == entry.searchText.npos)) {
            continue;
        }
        result.push_back(index);
    }
    return result;
}
ComponentAddAvailability ComponentBrowserModel::Availability(const EditorContext& context, const TypeDescriptor& type,
    std::span<const PersistentId> targets) {
    if(context.session.IsPlaying()) {
        return {false, "componentBrowser.running"};
    }
    if(!type.add && !type.addInstance) {
        return {false, "componentBrowser.required"};
    }
    auto* level = context.session.GetLevel();
    if(!level || targets.empty()) {
        return {false, "componentBrowser.noSelection"};
    }
    bool missing = false;
    for(const auto id : targets) {
        const auto entity = level->FindEntity(id);
        if(!entity.IsAlive()) {
            return {false, "componentBrowser.noSelection"};
        }
        // 既存の単一Componentは変更せず、不足している対象だけ追加する。
        const bool needsAdd = type.addInstance || !type.contains(entity);
        if(needsAdd && type.addUnavailableReason) {
            const auto reason = type.addUnavailableReason(entity);
            if(!reason.empty()) {
                return {false, reason};
            }
        }
        missing |= needsAdd;
    }
    return missing ? ComponentAddAvailability{true, ""} : ComponentAddAvailability{false, "componentBrowser.alreadyAdded"};
}
}
