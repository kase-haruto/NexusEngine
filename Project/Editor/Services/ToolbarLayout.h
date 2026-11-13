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
// x: 左端または右端からの距離、y: 上端からの距離、最後の値: 右端基準なら1。
using ToolbarPlacement = std::array<float, 3>;
using ToolbarPlacements = std::map<std::string, ToolbarPlacement>;
inline bool IsValidToolbarPlacement(const ToolbarPlacement& value) {
    return std::isfinite(value[0]) && std::isfinite(value[1]) && value[0] >= 0 && value[1] >= 0 &&
        value[0] <= 16384 && value[1] <= 4096 && (value[2] == 0 || value[2] == 1);
}

inline constexpr float kToolbarOffsetLimitX = 16384;
inline constexpr float kToolbarOffsetLimitY = 4096;

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
