#pragma once
#include "Foundation/Reflection/ReflectionMarkers.h"
#include <cmath>

NEXUS_REFLECT_COMPONENT(NexusGame::MoveComponent, "Game.Move", "component.gameMove", speed)

namespace NexusGame {
/** ゲーム固有の移動速度。Reflection宣言から追加一覧・Inspector・Level保存へ接続する。 */
struct MoveComponent {
    float speed = 3.0f; //< X方向へ毎秒移動する距離
    [[nodiscard]] bool IsValid() const noexcept { return std::isfinite(speed); }
};
}
