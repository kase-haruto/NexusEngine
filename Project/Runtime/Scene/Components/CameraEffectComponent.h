#pragma once
#include "Foundation/Reflection/ReflectionMarkers.h"
#include <array>
#include <string>
#include <string_view>

NEXUS_REFLECT_COMPONENT(CameraEffectComponent, "CameraEffect", "component.cameraEffect", shader, enabled, parameters0, parameters1, parameters2, parameters3)
NEXUS_COMPONENT_EDITOR(CameraEffectComponent, "Camera", "componentBrowser.description.cameraEffect", "details.Camera")

namespace NexusEngine {
/** \brief primary Cameraの表示効果。Curve/clipは通常の数値propertyとしてparameterを制御する。 */
struct CameraEffectComponent {
    std::string shader = "PostProcess/ColorAdjust.PS.hlsl";
    bool enabled = true;
    std::array<float, 4> parameters0{1, 1, 0, 0}; //< 表示色の明るさ倍率、saturation、vignette強度、未使用
    std::array<float, 4> parameters1{}, parameters2{}, parameters3{};
    bool IsValid() const;
    static bool IsPropertyAnimatable(std::string_view id);
};
}
