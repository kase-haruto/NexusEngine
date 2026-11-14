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
    const auto integerChoices = [](const UI::ItemOptions& options, PropertyValue& value, const PropertyDescriptor& property, const LocalizationService&) {
        auto* integer = std::get_if<int>(&value);
        if(!integer) {
            return UI::ItemResult{};
        }
        std::vector<std::string> labels;
        std::vector<UI::EnumOption> choices;
        if(property.kind == PropertyKind::CollisionLayer) {
            for(int layer = 0; layer < 32; ++layer) {
                labels.push_back(layer == 0 ? "0 (Default)" : "Layer " + std::to_string(layer));
            }
        } else {
            for(const auto& [number, label] : property.enumValues) {
                static_cast<void>(number);
                labels.push_back(label);
            }
        }
        for(size_t index = 0; index < labels.size(); ++index) {
            choices.push_back({property.kind == PropertyKind::CollisionLayer ? static_cast<int>(index) : property.enumValues[index].first, labels[index]});
        }
        return UI::PropertyEnum(options, *integer, choices);
    };
    Register(PropertyKind::ColliderShape, integerChoices);
    Register(PropertyKind::CollisionLayer, integerChoices);
    Register(PropertyKind::LayerMask, [](const auto& options, auto& value, const auto&, const auto&) {
        auto* integer = std::get_if<int>(&value);
        if(!integer) {
            return UI::ItemResult{};
        }
        bool changed = false;
        ImGui::PushID(static_cast<int>(options.id.value));
        ImGui::SetNextItemWidth(options.size.x);
        uint32_t mask = static_cast<uint32_t>(*integer);
        const auto preview = mask == UINT32_MAX ? "Everything" : mask == 0 ? "Nothing" : "Selected layers";
        if(ImGui::BeginCombo("##LayerMask", preview)) {
            if(ImGui::Selectable("Everything")) {
                mask = UINT32_MAX;
                changed = true;
            }
            if(ImGui::Selectable("Nothing")) {
                mask = 0;
                changed = true;
            }
            for(uint32_t layer = 0; layer < 32; ++layer) {
                const auto bit = uint32_t{1} << layer;
                const auto label = "Layer " + std::to_string(layer);
                if(ImGui::MenuItem(label.c_str(), nullptr, (mask & bit) != 0)) {
                    mask ^= bit;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
        if(changed) {
            *integer = static_cast<int32_t>(mask);
        }
        return UI::ItemResult{changed ? UI::ItemStatus::Edited | UI::ItemStatus::EditCompleted : UI::ItemStatus::None};
    });
    Register(PropertyKind::Rotation, [](const auto& options, auto& value, const auto&, const auto&) {
        auto* typed = std::get_if<std::array<float, 4>>(&value);
        return typed ? UI::PropertyRotation(options, *typed) : UI::ItemResult{};
    });
    Register(PropertyKind::Color, [](const auto& options, auto& value, const auto&, const auto&) {
        auto* typed = std::get_if<std::array<float, 4>>(&value);
        return typed ? UI::PropertyColor(options, *typed) : UI::ItemResult{};
    });
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
