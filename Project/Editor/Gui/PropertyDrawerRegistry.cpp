#include "PropertyDrawerRegistry.h"
#include "Editor/Gui/Widgets/PropertyWidgets.h"
#include "ThirdParty/imgui/imgui.h"

namespace NexusEngine {
UI::ItemResult PropertyDrawerRegistry::Draw(const UI::ItemOptions& options, PropertyValue& value,
    const PropertyDescriptor& property, const LocalizationService& text) const {
    auto it = drawers_.find(property.kind);
    if(it != drawers_.end()) return it->second(options, value, property, text);
    ImGui::TextUnformatted(text.Text("property.unsupported").c_str());
    return {};
}
void PropertyDrawerRegistry::RegisterDefaults() {
    // ReflectionとWidget間の値変換だけを担当し、入力UIはWidgetsへ集約する。
    Register(PropertyKind::Bool, [](const auto& options, auto& value, const auto&, const auto&) {
        auto* typed = std::get_if<bool>(&value);
        return typed ? UI::PropertyBool(options, *typed) : UI::ItemResult{};
    });
    Register(PropertyKind::Int, [](const auto& options, auto& value, const auto&, const auto&) {
        auto* typed = std::get_if<int>(&value);
        return typed ? UI::PropertyInt(options, *typed) : UI::ItemResult{};
    });
    Register(PropertyKind::Float, [](const auto& options, auto& value, const auto&, const auto&) {
        auto* typed = std::get_if<float>(&value);
        return typed ? UI::PropertyFloat(options, *typed) : UI::ItemResult{};
    });
    Register(PropertyKind::String, [](const auto& options, auto& value, const auto&, const auto&) {
        auto* typed = std::get_if<std::string>(&value);
        return typed ? UI::PropertyText(options, *typed) : UI::ItemResult{};
    });
    Register(PropertyKind::Vector2, [](const auto& options, auto& value, const auto&, const auto&) {
        auto* typed = std::get_if<std::array<float, 2>>(&value);
        return typed ? UI::PropertyVector(options, std::span<float>{*typed}) : UI::ItemResult{};
    });
    Register(PropertyKind::Vector3, [](const auto& options, auto& value, const auto&, const auto&) {
        auto* typed = std::get_if<std::array<float, 3>>(&value);
        return typed ? UI::PropertyVector(options, std::span<float>{*typed}) : UI::ItemResult{};
    });
    Register(PropertyKind::Vector4, [](const auto& options, auto& value, const auto&, const auto&) {
        auto* typed = std::get_if<std::array<float, 4>>(&value);
        return typed ? UI::PropertyVector(options, std::span<float>{*typed}) : UI::ItemResult{};
    });
    Register(PropertyKind::Enum, [](const auto& options, auto& value, const auto& descriptor, const auto& text) {
        auto* typed = std::get_if<EnumValue>(&value);
        if(!typed) return UI::ItemResult{};
        std::vector<std::string> labels;
        labels.reserve(descriptor.enumValues.size());
        for(const auto& [number, key] : descriptor.enumValues) {
            static_cast<void>(number);
            labels.push_back(text.Text(key));
        }
        // 全表示名の構築後にstring_viewを作り、vector再確保で参照を無効化しない。
        std::vector<UI::EnumOption> choices;
        choices.reserve(labels.size());
        for(size_t index = 0; index < labels.size(); ++index)
            choices.push_back({descriptor.enumValues[index].first, labels[index]});
        return UI::PropertyEnum(options, typed->value, choices);
    });
}
}
