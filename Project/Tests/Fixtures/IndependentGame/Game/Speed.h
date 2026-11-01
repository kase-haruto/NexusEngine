#pragma once
#include "Foundation/Reflection/ReflectionMarkers.h"
namespace IndependentGame {
/** Game側だけに定義する、移動速度の保存Component。 */
struct Speed { float value = 2; };
}
NEXUS_REFLECT_COMPONENT(IndependentGame::Speed, "Independent.Speed", "component.speed", value)
