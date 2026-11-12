#pragma once
#include <array>
#include <cstdint>
#include "GamepadFrame.h"

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * InputFrame
 * - 所有Windowから一度だけ消費する入力値。Native WindowやEditorへの参照を持たない
 * - edgeはOS messageから生成し、同じframeの複数Scriptが同じpressed/releasedを観測する
 *---------------------------------------------------------------------------------------*/
struct InputFrame {
    std::array<uint8_t, 256> down{}, pressed{}, released{}; //< Windows virtual key/button値。0/1だけを格納する
    float mouseX = 0, mouseY = 0, deltaX = 0, deltaY = 0, wheel = 0; //< client座標とframe差分。wheelはdetent単位
    bool focused = false; //< focusを失ったWindowのゲーム入力は使用しない
    std::array<GamepadFrame,4> gamepads{}; //< Window所有threadで一度採取する4台分。別Input Managerを作らない
    /** \brief 同じframe内の重複keydown/repeatを押下edgeとして再生成しない */
    void Key(uint32_t key, bool held) noexcept {
        if(key >= down.size()) return;
        if(held && !down[key]) pressed[key] = 1;
        if(!held && down[key]) released[key] = 1;
        down[key] = held ? 1 : 0;
    }
    /** \brief focus喪失時に保持状態を解除し、再focus後の押しっぱなしを防ぐ */
    void LoseFocus() noexcept {
        for(uint32_t key = 0; key < down.size(); ++key) Key(key, false);
        pressed.fill(0); deltaX = deltaY = wheel = 0; focused = false;
        for(auto& pad:gamepads) pad.LoseFocus();
    }
    /** \brief 消費したedge/deltaだけを消す。保持状態と現在座標は次frameへ引き継ぐ */
    void ClearTransitions() noexcept {
        pressed.fill(0); released.fill(0); deltaX = deltaY = wheel = 0;
        for(auto& pad:gamepads) pad.ClearTransitions();
    }
};
}
