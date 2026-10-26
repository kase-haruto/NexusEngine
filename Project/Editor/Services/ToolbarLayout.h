#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <string>

namespace NexusEngine {
// オフセットはフォントサイズ18pxを基準とした論理ピクセルで保持する。
// 描画時にUI倍率へ変換するため、保存値は画面のDPIに依存しない。
using ToolbarOffset = std::array<float, 2>;
using ToolbarOffsets = std::map<std::string, ToolbarOffset>;

inline constexpr float kToolbarOffsetLimitX = 1024;
inline constexpr float kToolbarOffsetLimitY = 256;

inline bool IsValidToolbarOffset(const ToolbarOffset& offset) {
    return std::isfinite(offset[0]) && std::isfinite(offset[1]);
}

inline ToolbarOffset ClampToolbarOffset(const ToolbarOffset& offset) {
    return {
        std::clamp(offset[0], -kToolbarOffsetLimitX, kToolbarOffsetLimitX),
        std::clamp(offset[1], -kToolbarOffsetLimitY, kToolbarOffsetLimitY)
    };
}
}
