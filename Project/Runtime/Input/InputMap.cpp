#include "InputMap.h"
#include "Runtime/Project/ProjectJson.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <stdexcept>

namespace NexusEngine {
namespace {
Error InvalidMap(const std::string& message) {
    return Error(ErrorCategory::Resource, 280, "InputMap: " + message);
}
bool NameValid(std::string_view name, bool empty = false) noexcept {
    if(name.size() > 63 || (!empty && name.empty())) {
        return false;
    }
    for(unsigned char value : name) {
        if(value <= 32 || value == 127) {
            return false;
        }
    }
    return true;
}
struct BindingValue {
    float value = 0;
    bool pressed = false;
};
/** OSを再pollしない。Game Viewのfocus抑止済みsnapshotだけを使う。 */
BindingValue ReadBinding(const InputBinding& binding, const InputFrame& frame) noexcept {
    if(!frame.focused) {
        return {};
    }
    if(binding.source == InputBindingSource::Key) {
        return {frame.down[binding.code] ? 1.f : 0.f, frame.pressed[binding.code] != 0};
    }
    if(binding.source == InputBindingSource::MouseX) {
        return {frame.deltaX, false};
    }
    if(binding.source == InputBindingSource::MouseY) {
        return {frame.deltaY, false};
    }
    if(binding.source == InputBindingSource::Wheel) {
        return {frame.wheel, false};
    }
    const auto& pad = frame.gamepads[binding.slot];
    if(!pad.connected) {
        return {};
    }
    switch(binding.source) {
    case InputBindingSource::GamepadButton:
        return {(pad.down & binding.code) ? 1.f : 0.f, (pad.pressed & binding.code) != 0};
    case InputBindingSource::LeftX: return {pad.leftStick[0], false};
    case InputBindingSource::LeftY: return {pad.leftStick[1], false};
    case InputBindingSource::RightX: return {pad.rightStick[0], false};
    case InputBindingSource::RightY: return {pad.rightStick[1], false};
    case InputBindingSource::LeftTrigger: return {pad.leftTrigger, false};
    case InputBindingSource::RightTrigger: return {pad.rightTrigger, false};
    default: return {};
    }
}
}
bool InputMap::IsValid() const noexcept {
    if(actions.size() > NEXUS_INPUT_MAX_ACTIONS) {
        return false;
    }
    for(size_t index = 0; index < actions.size(); ++index) {
        const auto& action = actions[index];
        if(!NameValid(action.name) || !NameValid(action.context, true) ||
            static_cast<uint32_t>(action.type) > 2 || !std::isfinite(action.deadZone) ||
            action.deadZone < 0 || action.deadZone >= 1 || action.bindings.size() > 32) {
            return false;
        }
        for(size_t previous = 0; previous < index; ++previous) {
            if(actions[previous].name == action.name) {
                return false;
            }
        }
        for(const auto& binding : action.bindings) {
            if(static_cast<uint32_t>(binding.source) > 10 || binding.slot >= 4 ||
                !std::isfinite(binding.scale[0]) || !std::isfinite(binding.scale[1]) ||
                std::abs(binding.scale[0]) > 100 || std::abs(binding.scale[1]) > 100) {
                return false;
            }
            if(binding.source == InputBindingSource::Key && binding.code > 255) {
                return false;
            }
            if(binding.source == InputBindingSource::GamepadButton &&
                (binding.code == 0 || (binding.code & ~0xf3ffu) || (binding.code & (binding.code - 1)))) {
                return false;
            }
        }
    }
    return true;
}
InputMap InputMap::Defaults() {
    InputMap map;
    map.actions = {
        {"Jump", "Gameplay", InputActionType::Button, .2f,
            {{InputBindingSource::Key, 32}, {InputBindingSource::GamepadButton, NEXUS_PAD_A}}},
        {"Attack", "Gameplay", InputActionType::Button, .2f,
            {{InputBindingSource::Key, 1}, {InputBindingSource::GamepadButton, NEXUS_PAD_X}}},
        {"Move", "Gameplay", InputActionType::Axis2D, .2f,
            {{InputBindingSource::Key, 'A', 0, {-1, 0}}, {InputBindingSource::Key, 'D', 0, {1, 0}},
             {InputBindingSource::Key, 'W', 0, {0, 1}}, {InputBindingSource::Key, 'S', 0, {0, -1}},
             {InputBindingSource::LeftX, 0, 0, {1, 0}}, {InputBindingSource::LeftY, 0, 0, {0, 1}}}},
        {"Look", "Gameplay", InputActionType::Axis2D, .05f,
            {{InputBindingSource::RightX, 0, 0, {1, 0}}, {InputBindingSource::RightY, 0, 0, {0, 1}}}},
        {"Pause", "", InputActionType::Button, .2f,
            {{InputBindingSource::Key, 27}, {InputBindingSource::GamepadButton, NEXUS_PAD_START}}},
        {"Confirm", "Menu", InputActionType::Button, .2f,
            {{InputBindingSource::Key, 13}, {InputBindingSource::GamepadButton, NEXUS_PAD_A}}},
        {"Cancel", "Menu", InputActionType::Button, .2f,
            {{InputBindingSource::Key, 27}, {InputBindingSource::GamepadButton, NEXUS_PAD_B}}}
    };
    return map;
}
Result<InputMap> InputMap::Load(const std::filesystem::path& file) {
    std::error_code error;
    const bool exists = std::filesystem::exists(file, error);
    if(error) {
        return std::unexpected(InvalidMap("Cannot inspect settings file."));
    }
    if(!exists) {
        return Defaults();
    }
    auto document = ProjectJson::Read(file);
    if(!document) {
        return std::unexpected(document.error());
    }
    try {
        const auto& root = *document;
        ProjectJson::Keys(root, {"version", "actions"});
        if(!root.at("version").is_number_integer() || root.at("version") != 1 || !root.at("actions").is_array() || root.at("actions").size() > NEXUS_INPUT_MAX_ACTIONS) {
            throw std::runtime_error("Unsupported version or action count.");
        }
        InputMap map;
        for(const auto& entry : root.at("actions")) {
            ProjectJson::Keys(entry, {"name", "context", "type", "deadZone", "bindings"});
            InputAction action;
            action.name = ProjectJson::Text(entry, "name");
            action.context = ProjectJson::Text(entry, "context", true);
            const auto type = entry.at("type").get<std::string>();
            if(type != "Button" && type != "Axis1D" && type != "Axis2D") {
                throw std::runtime_error("Invalid action type.");
            }
            action.type = type == "Button" ? InputActionType::Button : type == "Axis1D" ? InputActionType::Axis1D : InputActionType::Axis2D;
            action.deadZone = entry.at("deadZone").get<float>();
            if(!entry.at("bindings").is_array() || entry.at("bindings").size() > 32) {
                throw std::runtime_error("Invalid binding count.");
            }
            for(const auto& item : entry.at("bindings")) {
                ProjectJson::Keys(item, {"source", "code", "slot", "scale"});
                // 数値の負値・小数・整数overflowをenumへの変換前に拒否する。
                for(const char* key : {"source", "code", "slot"}) {
                    if(!item.at(key).is_number_integer() || item.at(key).get<int64_t>() < 0 || item.at(key).get<uint64_t>() > UINT32_MAX) {
                        throw std::runtime_error("Invalid binding integer.");
                    }
                }
                if(!item.at("scale").is_array() || item.at("scale").size() != 2) {
                    throw std::runtime_error("A binding scale must have two components.");
                }
                action.bindings.push_back({static_cast<InputBindingSource>(item.at("source").get<uint32_t>()),
                    item.at("code").get<uint32_t>(), item.at("slot").get<uint32_t>(), item.at("scale").get<std::array<float, 2>>()});
            }
            map.actions.push_back(std::move(action));
        }
        if(!map.IsValid()) {
            throw std::runtime_error("Invalid names, duplicates, values or bindings.");
        }
        return map;
    } catch(const std::exception& exception) {
        return std::unexpected(InvalidMap(exception.what()));
    }
}
Result<void> InputMap::Save(const std::filesystem::path& file) const {
    if(!IsValid() || file.empty()) {
        return std::unexpected(InvalidMap("Invalid settings or file path."));
    }
    std::error_code error;
    if(!file.parent_path().empty()) {
        std::filesystem::create_directories(file.parent_path(), error);
    }
    if(error) {
        return std::unexpected(InvalidMap("Cannot create settings directory."));
    }
    ProjectJson::Json entries = ProjectJson::Json::array();
    for(const auto& action : actions) {
        auto bindings = ProjectJson::Json::array();
        for(const auto& binding : action.bindings) {
            bindings.push_back({{"source", static_cast<uint32_t>(binding.source)}, {"code", binding.code},
                {"slot", binding.slot}, {"scale", binding.scale}});
        }
        const char* type = action.type == InputActionType::Button ? "Button" : action.type == InputActionType::Axis1D ? "Axis1D" : "Axis2D";
        entries.push_back({{"name", action.name}, {"context", action.context}, {"type", type},
            {"deadZone", action.deadZone}, {"bindings", std::move(bindings)}});
    }
    return ProjectJson::Write(file, {{"version", 1}, {"actions", std::move(entries)}});
}
InputMapRuntime::InputMapRuntime() {
    Configure(InputMap::Defaults());
}
void InputMapRuntime::Configure(InputMap map) {
    // 検証済み候補だけを公開する。再構成時は旧sessionの保持・edgeを引き継がない。
    if(!map.IsValid()) {
        throw std::invalid_argument("Invalid Input Map.");
    }
    map_ = std::move(map);
    frame_ = {};
    frame_.count = static_cast<uint32_t>(map_.actions.size());
    context_.fill(0);
    pendingContext_.fill(0);
    std::memcpy(context_.data(), "Gameplay", 8);
    pendingContext_ = context_;
    std::memcpy(frame_.context, context_.data(), context_.size());
    for(size_t index = 0; index < map_.actions.size(); ++index) {
        std::memcpy(frame_.actions[index].name, map_.actions[index].name.data(), map_.actions[index].name.size());
        frame_.actions[index].kind = static_cast<uint32_t>(map_.actions[index].type);
    }
}
void InputMapRuntime::Advance(const InputFrame& input) noexcept {
    const bool contextChanged = context_ != pendingContext_;
    context_ = pendingContext_;
    std::memcpy(frame_.context, context_.data(), context_.size());
    frame_.focused = input.focused ? 1u : 0u;
    for(size_t index = 0; index < map_.actions.size(); ++index) {
        const auto& action = map_.actions[index];
        auto& state = frame_.actions[index];
        const bool previous = state.down != 0;
        // 有限だが大きなmouse deltaでも途中計算をoverflowさせない。
        double x = 0, y = 0;
        bool tap = false;
        if(input.focused && (action.context.empty() || action.context == context_.data())) {
            for(const auto& binding : action.bindings) {
                const auto sample = ReadBinding(binding, input);
                if(!std::isfinite(sample.value)) {
                    continue;
                }
                const double bx = static_cast<double>(sample.value) * binding.scale[0];
                const double by = static_cast<double>(sample.value) * binding.scale[1];
                if(action.type == InputActionType::Button) {
                    // Buttonは各bindingのOR。複数の弱いtriggerを足して押下にしない。
                    x = std::max(x, std::abs(bx));
                } else {
                    x += bx;
                    y += by;
                }
                tap |= sample.pressed && std::abs(binding.scale[0]) > action.deadZone;
            }
        }
        if(action.type != InputActionType::Axis2D) {
            y = 0;
        }
        const double magnitude = std::sqrt(x * x + y * y);
        const bool down = magnitude > action.deadZone;
        if(!down) {
            x = y = 0;
        } else if(magnitude > 1) {
            // 斜め移動と複数device同時入力の速度を単位長へ制限する。
            x /= magnitude;
            y /= magnitude;
        }
        // メニューを開いたEscapeの保持を、新ContextのCancel押下として再発火しない。
        // 切替後frameで実際に押したキーのtapは、そのframeから受け付ける。
        const bool newlyActive = contextChanged && !action.context.empty() && action.context == context_.data();
        const bool pressed = !previous && ((down && !newlyActive) || (action.type == InputActionType::Button && tap));
        state.down = down ? 1u : 0u;
        state.pressed = pressed ? 1u : 0u;
        state.released = (previous || pressed) && !down ? 1u : 0u;
        state.value[0] = action.type == InputActionType::Button ? (down ? 1.f : 0.f) : static_cast<float>(x);
        state.value[1] = static_cast<float>(y);
    }
}
bool InputMapRuntime::RequestContext(std::string_view context) noexcept {
    if(!NameValid(context)) {
        return false;
    }
    bool found = context == "Gameplay";
    for(const auto& action : map_.actions) {
        found |= action.context == context;
    }
    if(!found) {
        return false;
    }
    pendingContext_.fill(0);
    std::memcpy(pendingContext_.data(), context.data(), context.size());
    return true;
}
const NexusInputActionsFrameV1& InputMapRuntime::Frame() const noexcept {
    return frame_;
}
const InputMap& InputMapRuntime::Map() const noexcept {
    return map_;
}
}
