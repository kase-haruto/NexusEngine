#include "InputMapPanel.h"
#include "Editor/Core/EditorSession.h"
#include "Editor/Services/LocalizationService.h"
#include "ThirdParty/imgui/imgui.h"
#include <cstring>
#include <algorithm>

namespace NexusEngine {
namespace {
/** 可変長文字列の編集は固定長UTF-8 bufferを経由し、保存上限とUIを一致させる。 */
bool EditName(const char* label, std::string& value) {
    std::array<char, 64> buffer {};
    std::memcpy(buffer.data(), value.data(), std::min(value.size(), buffer.size() - 1));
    if(ImGui::InputText(label, buffer.data(), buffer.size())) {
        value = buffer.data();
        return true;
    }
    return false;
}
const char* SpecialKey(uint32_t code) {
    switch(code) {
    case 1: return "Mouse Left";
    case 2: return "Mouse Right";
    case 4: return "Mouse Middle";
    case 5: return "Mouse X1";
    case 6: return "Mouse X2";
    case 8: return "Backspace";
    case 9: return "Tab";
    case 13: return "Enter";
    case 16: return "Shift";
    case 17: return "Ctrl";
    case 18: return "Alt";
    case 27: return "Escape";
    case 32: return "Space";
    case 37: return "Left";
    case 38: return "Up";
    case 39: return "Right";
    case 40: return "Down";
    default: return nullptr;
    }
}
std::string KeyLabel(uint32_t code) {
    if(const auto* special = SpecialKey(code)) {
        return special;
    }
    if((code >= 'A' && code <= 'Z') || (code >= '0' && code <= '9')) {
        return std::string(1, static_cast<char>(code));
    }
    if(code >= 112 && code <= 123) {
        return "F" + std::to_string(code - 111);
    }
    return "VK " + std::to_string(code);
}
void KeyCombo(uint32_t& code) {
    if(ImGui::BeginCombo("##key", KeyLabel(code).c_str())) {
        for(uint32_t index = 0; index < 256; ++index) {
            if(!SpecialKey(index) && !(index >= 'A' && index <= 'Z') &&
                !(index >= '0' && index <= '9') && !(index >= 112 && index <= 123)) {
                continue;
            }
            if(ImGui::Selectable(KeyLabel(index).c_str(), code == index)) {
                code = index;
            }
        }
        ImGui::EndCombo();
    }
}
}
InputMapPanel::InputMapPanel(std::filesystem::path file) : file_(std::move(file)) {
    Reload();
}
std::string_view InputMapPanel::Id() const noexcept {
    return "InputMap";
}
std::string_view InputMapPanel::TitleKey() const noexcept {
    return "inputMap.title";
}
std::array<float, 2> InputMapPanel::InitialSize() const noexcept {
    return {850, 680};
}
void InputMapPanel::Reload() {
    auto loaded = InputMap::Load(file_);
    loaded_ = loaded.has_value();
    if(!loaded) {
        error_ = loaded.error().GetMessageText();
        return;
    }
    std::error_code error;
    persisted_ = std::filesystem::exists(file_, error) && !error;
    saved_ = *loaded;
    draft_ = *loaded;
    error_.clear();
}
void InputMapPanel::Save() {
    // 外部編集を暗黙に上書きしない。失敗時はdraftを保持して修正・再読込できる。
    auto current = InputMap::Load(file_);
    if(!current || *current != saved_) {
        error_ = "Input Map changed externally. Reload before saving.";
        return;
    }
    auto result = draft_.Save(file_);
    if(!result) {
        error_ = result.error().GetMessageText();
        return;
    }
    saved_ = draft_;
    persisted_ = true;
    error_.clear();
}
void InputMapPanel::Draw(const EditorContext& context) {
    const auto text = [&](const char* key) {
        return context.text.Text(key);
    };
    ImGui::TextWrapped("%s", text("inputMap.help").c_str());
    ImGui::TextDisabled("%s", file_.string().c_str());
    ImGui::BeginDisabled(context.session.IsPlaying());
    ImGui::BeginDisabled(!loaded_ || !draft_.IsValid() || (draft_ == saved_ && persisted_));
    if(ImGui::Button(text("file.save").c_str())) {
        Save();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if(ImGui::Button(text("inputMap.reload").c_str())) {
        Reload();
    }
    ImGui::SameLine();
    if(ImGui::Button(text("inputMap.defaults").c_str()) && loaded_) {
        draft_ = InputMap::Defaults();
    }
    if(!error_.empty()) {
        ImGui::TextWrapped("%s", error_.c_str());
    }
    if(loaded_ && !draft_.IsValid()) {
        ImGui::TextWrapped("%s", text("inputMap.invalid").c_str());
    }
    ImGui::BeginDisabled(!loaded_);
    ImGui::Text("%zu / %u", draft_.actions.size(), NEXUS_INPUT_MAX_ACTIONS);
    ImGui::BeginDisabled(draft_.actions.size() >= NEXUS_INPUT_MAX_ACTIONS);
    if(ImGui::Button(text("inputMap.addAction").c_str())) {
        // 初期名の重複は設定の上限まで探索し、追加直後も保存できる状態を保つ。
        for(uint32_t suffix = 1; suffix <= NEXUS_INPUT_MAX_ACTIONS + 1; ++suffix) {
            const auto name = "Action" + std::to_string(suffix);
            const bool used = std::ranges::any_of(draft_.actions, [&](const auto& action) {
                return action.name == name;
            });
            if(!used) {
                draft_.actions.push_back({name, "Gameplay", InputActionType::Button, .2f, {InputBinding{}}});
                break;
            }
        }
    }
    ImGui::EndDisabled();
    for(size_t index = 0; index < draft_.actions.size(); ++index) {
        auto& action = draft_.actions[index];
        ImGui::PushID(static_cast<int>(index));
        const bool open = ImGui::TreeNodeEx("##action", ImGuiTreeNodeFlags_DefaultOpen, "%s", action.name.c_str());
        ImGui::SameLine();
        if(ImGui::SmallButton(text("inputMap.remove").c_str())) {
            draft_.actions.erase(draft_.actions.begin() + static_cast<ptrdiff_t>(index));
            if(open) {
                ImGui::TreePop();
            }
            ImGui::PopID();
            break;
        }
        if(open) {
            ImGui::SetNextItemWidth(220);
            EditName(text("inputMap.name").c_str(), action.name);
            ImGui::SetNextItemWidth(220);
            EditName(text("inputMap.context").c_str(), action.context);
            int type = static_cast<int>(action.type);
            ImGui::SetNextItemWidth(220);
            if(ImGui::Combo(text("inputMap.type").c_str(), &type, "Button\0Axis1D\0Axis2D\0")) {
                action.type = static_cast<InputActionType>(type);
            }
            ImGui::SetNextItemWidth(220);
            ImGui::SliderFloat(text("inputMap.deadZone").c_str(), &action.deadZone, 0, .99f);
            for(size_t bindingIndex = 0; bindingIndex < action.bindings.size(); ++bindingIndex) {
                auto& binding = action.bindings[bindingIndex];
                ImGui::PushID(static_cast<int>(bindingIndex));
                int source = static_cast<int>(binding.source);
                ImGui::SetNextItemWidth(155);
                if(ImGui::Combo("##source", &source,
                    "Keyboard / Mouse\0Gamepad Button\0Left Stick X\0Left Stick Y\0Right Stick X\0Right Stick Y\0Left Trigger\0Right Trigger\0Mouse Delta X\0Mouse Delta Y\0Mouse Wheel\0")) {
                    binding.source = static_cast<InputBindingSource>(source);
                    binding.code = source == 1 ? NEXUS_PAD_A : source == 0 ? 32u : 0u;
                }
                if(source == 0) {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(120);
                    KeyCombo(binding.code);
                    ImGui::SameLine();
                    int code = static_cast<int>(binding.code);
                    ImGui::SetNextItemWidth(65);
                    if(ImGui::InputInt("VK", &code, 0)) {
                        binding.code = static_cast<uint32_t>(std::clamp(code, 0, 255));
                    }
                } else if(source <= 7) {
                    if(source == 1) {
                        const uint32_t buttons[] = {NEXUS_PAD_A, NEXUS_PAD_B, NEXUS_PAD_X, NEXUS_PAD_Y,
                            NEXUS_PAD_DPAD_UP, NEXUS_PAD_DPAD_DOWN, NEXUS_PAD_DPAD_LEFT, NEXUS_PAD_DPAD_RIGHT,
                            NEXUS_PAD_START, NEXUS_PAD_BACK, NEXUS_PAD_LEFT_SHOULDER, NEXUS_PAD_RIGHT_SHOULDER,
                            NEXUS_PAD_LEFT_STICK, NEXUS_PAD_RIGHT_STICK};
                        int selected = 0;
                        for(int button = 0; button < 14; ++button) {
                            if(buttons[button] == binding.code) {
                                selected = button;
                            }
                        }
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(120);
                        if(ImGui::Combo("##button", &selected, "A\0B\0X\0Y\0DPad Up\0DPad Down\0DPad Left\0DPad Right\0Start\0Back\0LB\0RB\0Left Stick\0Right Stick\0")) {
                            binding.code = buttons[selected];
                        }
                    }
                    int slot = static_cast<int>(binding.slot);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(60);
                    if(ImGui::InputInt("Pad", &slot, 0)) {
                        binding.slot = static_cast<uint32_t>(std::clamp(slot, 0, 3));
                    }
                }
                ImGui::SetNextItemWidth(180);
                ImGui::DragFloat2(text("inputMap.scale").c_str(), binding.scale.data(), .05f, -100, 100);
                ImGui::SameLine();
                if(ImGui::SmallButton(text("inputMap.removeBinding").c_str())) {
                    action.bindings.erase(action.bindings.begin() + static_cast<ptrdiff_t>(bindingIndex));
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            ImGui::BeginDisabled(action.bindings.size() >= 32);
            if(ImGui::SmallButton(text("inputMap.addBinding").c_str())) {
                action.bindings.emplace_back();
            }
            ImGui::EndDisabled();
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
}
bool InputMapPanel::HasUnsavedChanges() const {
    return loaded_ && draft_ != saved_;
}
Result<void> InputMapPanel::SaveDraft() {
    if (!loaded_) {
        Reload();
    }
    Save();
    if (!error_.empty()) {
        return std::unexpected(Error(ErrorCategory::Resource, 298, error_));
    }
    return {};
}

} // namespace NexusEngine
