#pragma once
#include <array>

namespace NexusEngine {
    /** ワールド空間の診断線。GPU資源やゲームObjectの参照を保持しない。 */
    struct DebugDrawLine {
        std::array<float, 3> start{}, end{};
        std::array<float, 4> color{1, 1, 1, 1};
        bool depthTest = true;
    };
}
