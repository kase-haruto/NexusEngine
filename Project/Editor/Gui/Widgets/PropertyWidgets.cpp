#include "PropertyWidgets.h"
#include "Editor/ImGui/ImGuiStyle.h"
#include "ThirdParty/imgui/imgui.h"
#include <algorithm>
#include <climits>
#include <cstring>
#include <vector>
#include "Foundation/Math/Quaternion.h"

namespace NexusEngine::UI {
namespace {
// 色・余白を各入力で再定義せず、Editor全体へ適用済みのStyleを継承する。
class PropertyScope final {
public:
    explicit PropertyScope(const ItemOptions& options) : label_(options.label) {
        ImGui::PushID(static_cast<int>(options.id.value >> 32));
        ImGui::PushID(static_cast<int>(options.id.value & 0xffffffffu));
        ImGui::BeginDisabled(!options.enabled);
        label_ += "###PropertyField";
        if(options.size.x > 0) ImGui::SetNextItemWidth(options.size.x);
    }
    ~PropertyScope() { ImGui::EndDisabled(); ImGui::PopID(); ImGui::PopID(); }
    const char* Label() const { return label_.c_str(); }
    PropertyScope(const PropertyScope&) = delete;
    PropertyScope& operator=(const PropertyScope&) = delete;
private:
    std::string label_; //< 言語切替が入力IDに影響しないローカル表示名
};
ItemResult Capture(bool changed) {
    ItemResult result;
    if(ImGui::IsItemHovered()) result.status = result.status | ItemStatus::Hovered;
    if(ImGui::IsItemActive()) result.status = result.status | ItemStatus::Active;
    if(ImGui::IsItemFocused()) result.status = result.status | ItemStatus::Focused;
    if(ImGui::IsItemActivated()) result.status = result.status | ItemStatus::Activated;
    if(ImGui::IsItemDeactivated()) result.status = result.status | ItemStatus::Deactivated;
    if(changed) result.status = result.status | ItemStatus::Edited;
    if(ImGui::IsItemDeactivatedAfterEdit() || (changed && !ImGui::IsItemActive()))
        result.status = result.status | ItemStatus::EditCompleted;
    return result;
}
void Tooltip(const ItemOptions& options) {
    if(!options.tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(options.tooltip.data(), options.tooltip.data() + options.tooltip.size());
        ImGui::EndTooltip();
    }
}
template<class Draw> ItemResult DrawProperty(const ItemOptions& options, Draw draw) {
    if(!options.visible || !options.id.IsValid()) return {};
    PropertyScope scope(options);
    const bool changed = draw(scope.Label());
    auto result = Capture(changed); // Tooltipやpopup描画前に編集状態を取得する。
    Tooltip(options);
    return result;
}
}
ItemResult PropertyBool(const ItemOptions& options, bool& value) {
    return DrawProperty(options, [&](const char* label) { return ImGui::Checkbox(label, &value); });
}
ItemResult PropertyInt(const ItemOptions& options, int& value, float speed) {
    return DrawProperty(options, [&](const char* label) { return ImGui::DragInt(label, &value, speed); });
}
ItemResult PropertyFloat(const ItemOptions& options, float& value, float speed) {
    return DrawProperty(options, [&](const char* label) { return ImGui::DragFloat(label, &value, speed); });
}
ItemResult PropertyText(const ItemOptions& options, std::string& value, size_t maxBytes) {
    if(value.size() >= static_cast<size_t>(INT_MAX - 1) || maxBytes >= static_cast<size_t>(INT_MAX - 1)) return {};
    return DrawProperty(options, [&](const char* label) {
        // string::capacityの未構築領域に書き込まず、終端を含むvector要素を使用する。
        std::vector<char> buffer(std::max(value.size(), maxBytes) + 1, '\0');
        std::memcpy(buffer.data(), value.data(), value.size());
        const auto flags = value.size() > maxBytes ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_None;
        if(ImGui::InputText(label, buffer.data(), buffer.size(), flags)) { value = buffer.data(); return true; }
        return false;
    });
}
ItemResult PropertyVector(const ItemOptions& options, std::span<float> values, float speed) {
    if(values.size() < 2 || values.size() > 4 || !options.visible || !options.id.IsValid()) return {};
    PropertyScope scope(options);
    const auto theme = CurrentEditorTheme();
    const Color axes[] = {theme.axisX, theme.axisY, theme.axisZ, theme.axisW};
    const char* formats[] = {"X: %.3f", "Y: %.3f", "Z: %.3f", "W: %.3f"};
    const auto& style = ImGui::GetStyle();
    const float width = ImGui::CalcItemWidth();
    const float count = static_cast<float>(values.size());
    // Keep values readable in narrow docked panels by stacking the components.
    const bool stacked = width < count * ImGui::GetFontSize() * 4.5f;
    const float fieldWidth = stacked ? width : (width - style.ItemInnerSpacing.x * (count - 1)) / count;
    ItemResult result;
    ImGui::BeginGroup();
    for(size_t index = 0; index < values.size(); ++index) {
        ImGui::PushID(static_cast<int>(index));
        if(index && !stacked) ImGui::SameLine(0, style.ItemInnerSpacing.x);
        ImGui::SetNextItemWidth(fieldWidth);
        const bool changed = ImGui::DragFloat("##axis", &values[index], speed, 0, 0, formats[index]);
        // Capture each real input before decoration/grouping changes LastItemData.
        result.status = result.status | Capture(changed).status;
        if(ImGui::IsItemVisible()) {
            const auto min = ImGui::GetItemRectMin();
            const auto max = ImGui::GetItemRectMax();
            const auto& axis = axes[index];
            const float thickness = std::max(1.0f, ImGui::GetFontSize() / 9.0f);
            ImGui::GetWindowDrawList()->AddRectFilled({min.x + style.FrameRounding, max.y - thickness},
                {max.x - style.FrameRounding, max.y},
                ImGui::GetColorU32(ImVec4(axis.red, axis.green, axis.blue, axis.alpha)));
        }
        Tooltip(options);
        ImGui::PopID();
    }
    ImGui::EndGroup();
    if(!options.label.empty()) {
        ImGui::SameLine(0, style.ItemInnerSpacing.x);
        ImGui::TextUnformatted(options.label.data(), options.label.data() + options.label.size());
    }
    return result;
}
ItemResult PropertyEnum(const ItemOptions& options, int& value, std::span<const EnumOption> choices) {
    if(!options.visible || !options.id.IsValid()) return {};
    PropertyScope scope(options);
    std::string preview = std::to_string(value);
    for(const auto& choice : choices) if(choice.value == value) preview = choice.label;
    const bool open = ImGui::BeginCombo(scope.Label(), preview.c_str());
    // popup項目で上書きされる前にCombo自身の状態を退避する。
    auto result = Capture(false);
    if(open) {
        for(const auto& choice : choices) {
            ImGui::PushID(choice.value);
            const auto label = std::string(choice.label) + "###EnumOption";
            if(ImGui::Selectable(label.c_str(), choice.value == value) && choice.value != value) {
                value = choice.value;
                result.status = result.status | ItemStatus::Activated | ItemStatus::Edited | ItemStatus::EditCompleted;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    } else Tooltip(options);
    return result;
}
ItemResult PropertyRotation(const ItemOptions& options, std::array<float, 4>& value) {
    const Quaternion q{value[0], value[1], value[2], value[3]};
    constexpr float degrees = 57.29577951308232f;
    std::array<float, 3> angles{
        std::atan2(2*(q.w*q.x + q.y*q.z), 1 - 2*(q.x*q.x + q.y*q.y)) * degrees,
        std::asin(std::clamp(2*(q.w*q.y - q.z*q.x), -1.0f, 1.0f)) * degrees,
        std::atan2(2*(q.w*q.z + q.x*q.y), 1 - 2*(q.y*q.y + q.z*q.z)) * degrees};
    const auto result = PropertyVector(options, angles, 0.25f);
    if(result.Changed()) {
        const auto rotation = Quaternion::FromEulerRadians({angles[0]/degrees, angles[1]/degrees, angles[2]/degrees});
        value = {rotation.x, rotation.y, rotation.z, rotation.w};
    }
    return result;
}
ItemResult PropertyColor(const ItemOptions& options, std::array<float, 4>& value) {
    return DrawProperty(options, [&](const char* label) { return ImGui::ColorEdit4(label, value.data()); });
}
}
