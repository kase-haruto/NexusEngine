#pragma once
#include <cstdint>

namespace NexusEngine {
    /** Runtime所有の時計。保存データやEditorの編集Worldへ持ち越さない。 */
    struct GameTimeState {
        double elapsed = 0, unscaledElapsed = 0, fixedElapsed = 0, accumulator = 0, droppedFixedTime = 0;
        float deltaTime = 0, unscaledDeltaTime = 0, timeScale = 1, fixedDeltaTime = 1.0f / 60;
        uint64_t frame = 0, fixedTick = 0;
        uint32_t pendingSteps = 0;
        float activeFixedDelta = 1.0f / 60; //< frame中の間隔変更を次frameまで保留する
        bool inFixedStep = false;
        /** \brief 一回の可変frameを確定する。最大64物理stepで追いつけない時間は明示的に記録する */
        bool BeginFrame(float unscaledDelta);
    };
}
