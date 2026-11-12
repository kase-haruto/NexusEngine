#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <Xinput.h>
#include "Foundation/Input/InputFrame.h"
#pragma comment(lib,"xinput.lib")

namespace NexusEngine {
/**
 * \brief Windowの所有threadで4台を一度だけpollする。失敗/切断時は前の値を再利用しない
 * \param frame 前frameの保持状態を含むWindow所有の入力値。Scriptから直接呼ばない
 */
inline void PollXInputGamepads(InputFrame& frame) noexcept {
    for(DWORD slot=0;slot<4;++slot) {
        XINPUT_STATE state{}; GamepadSample sample;
        // packet番号が同じでもedgeの終了とfocus変化を評価するため毎frame正規化する。
        if(XInputGetState(slot,&state)==ERROR_SUCCESS) {
            const auto& pad=state.Gamepad; sample.connected=true; sample.buttons=pad.wButtons;
            sample.leftX=pad.sThumbLX; sample.leftY=pad.sThumbLY; sample.rightX=pad.sThumbRX; sample.rightY=pad.sThumbRY;
            sample.leftTrigger=pad.bLeftTrigger; sample.rightTrigger=pad.bRightTrigger;
        }
        frame.gamepads[slot].Update(sample,frame.focused);
    }
}
}
