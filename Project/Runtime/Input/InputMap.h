#pragma once
#include "Foundation/Error/Result.h"
#include "Foundation/Input/InputFrame.h"
#include "Public/Nexus/GameInput.h"
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace NexusEngine {
/** 操作の値型。Buttonでも閾値を越えるアナログ入力を割り当てられる。 */
enum class InputActionType : uint32_t { Button, Axis1D, Axis2D };
enum class InputBindingSource : uint32_t {
    Key, GamepadButton, LeftX, LeftY, RightX, RightY,
    LeftTrigger, RightTrigger, MouseX, MouseY, Wheel
};
struct InputBinding {
    InputBindingSource source = InputBindingSource::Key;
    uint32_t code = 32;
    uint32_t slot = 0;
    std::array<float, 2> scale {1, 0};
    friend bool operator==(const InputBinding&, const InputBinding&) = default;
};
struct InputAction {
    std::string name;
    std::string context = "Gameplay"; //< 空文字は全Contextで有効
    InputActionType type = InputActionType::Button;
    float deadZone = .2f;
    std::vector<InputBinding> bindings;
    friend bool operator==(const InputAction&, const InputAction&) = default;
};
/** Project共通の定義。デバイス状態やWorldへの参照を保存しない。 */
struct InputMap {
    std::vector<InputAction> actions;
    bool IsValid() const noexcept;
    static InputMap Defaults();
    static Result<InputMap> Load(const std::filesystem::path& file);
    Result<void> Save(const std::filesystem::path& file) const;
    friend bool operator==(const InputMap&, const InputMap&) = default;
};
/** Levelごとの評価状態。1回のframe採取から全言語が同じ非消費snapshotを読む。 */
class InputMapRuntime {
public:
    InputMapRuntime();
    void Configure(InputMap map);
    void Advance(const InputFrame& input) noexcept;
    bool RequestContext(std::string_view context) noexcept;
    const NexusInputActionsFrameV1& Frame() const noexcept;
    const InputMap& Map() const noexcept;
private:
    InputMap map_;
    NexusInputActionsFrameV1 frame_ {};
    std::array<char, 64> context_ {}, pendingContext_ {};
};
}
