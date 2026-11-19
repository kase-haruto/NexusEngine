#include "GameInput.hpp"
#include <cstring>
#include <cmath>

namespace NexusGame {
const NexusInputActionStateV1* ActionFrame::Find(std::string_view name) const noexcept {
    for(uint32_t index = 0; index < frame_.count; ++index) {
        if(name == frame_.actions[index].name) {
            return &frame_.actions[index];
        }
    }
    return nullptr;
}
bool ActionFrame::Contains(std::string_view name) const noexcept {
    return Find(name) != nullptr;
}
bool ActionFrame::Down(std::string_view name) const noexcept {
    const auto* state = Find(name);
    return state && state->down != 0;
}
bool ActionFrame::Pressed(std::string_view name) const noexcept {
    const auto* state = Find(name);
    return state && state->pressed != 0;
}
bool ActionFrame::Released(std::string_view name) const noexcept {
    const auto* state = Find(name);
    return state && state->released != 0;
}
float ActionFrame::Value(std::string_view name) const noexcept {
    const auto* state = Find(name);
    return state ? state->value[0] : 0.f;
}
std::array<float, 2> ActionFrame::Vector2(std::string_view name) const noexcept {
    const auto* state = Find(name);
    if(!state) {
        return {};
    }
    return {state->value[0], state->value[1]};
}
std::string_view ActionFrame::Context() const noexcept {
    return frame_.context;
}
NexusGameStatus Input::ReadActions(ActionFrame& output) const noexcept {
    output = {};
    if(api_.abiVersion != 1 || api_.structSize != sizeof(api_) || !api_.invoke || !client_) {
        return NEXUS_GAME_INVALID_ARGUMENT;
    }
    NexusInputActionRequestV1 request {};
    request.operation = 2;
    NexusInputActionsFrameV1 next {};
    uint32_t length = 0;
    const auto status = api_.invoke(client_, {NEXUS_INPUT_COMMAND, sizeof(NEXUS_INPUT_COMMAND) - 1}, 1, {},
        {reinterpret_cast<const uint8_t*>(&request), sizeof(request)}, reinterpret_cast<uint8_t*>(&next), sizeof(next), &length);
    if(status != NEXUS_GAME_OK) {
        return status;
    }
    if(length != sizeof(next) || next.count > NEXUS_INPUT_MAX_ACTIONS || next.focused > 1 ||
        !std::memchr(next.context, 0, sizeof(next.context))) {
        return NEXUS_GAME_FAILED;
    }
    for(uint32_t index = 0; index < next.count; ++index) {
        const auto& state = next.actions[index];
        if(!std::memchr(state.name, 0, sizeof(state.name)) || state.kind > 2 ||
            state.down > 1 || state.pressed > 1 || state.released > 1 ||
            !std::isfinite(state.value[0]) || !std::isfinite(state.value[1])) {
            return NEXUS_GAME_FAILED;
        }
    }
    output.frame_ = next;
    return NEXUS_GAME_OK;
}
NexusGameStatus Input::SetContext(std::string_view context) const noexcept {
    if(api_.abiVersion != 1 || api_.structSize != sizeof(api_) || !api_.invoke || !client_ || context.empty() || context.size() > 63) {
        return NEXUS_GAME_INVALID_ARGUMENT;
    }
    NexusInputActionRequestV1 request {};
    request.operation = 3;
    for(unsigned char character : context) {
        if(character <= 32 || character == 127) {
            return NEXUS_GAME_INVALID_ARGUMENT;
        }
    }
    std::memcpy(request.context, context.data(), context.size());
    uint32_t length = 0;
    return api_.invoke(client_, {NEXUS_INPUT_COMMAND, sizeof(NEXUS_INPUT_COMMAND) - 1}, 1, {},
        {reinterpret_cast<const uint8_t*>(&request), sizeof(request)}, nullptr, 0, &length);
}
}
