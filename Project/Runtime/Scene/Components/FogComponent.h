#pragma once
#include "Foundation/Reflection/ReflectionMarkers.h"
#include "Graphics/Fog/FogSettings.h"
NEXUS_REFLECT_COMPONENT(FogComponent, "Fog", "component.fog", enabled, startDistance, endDistance, color)

namespace NexusEngine {
    // Scene-wide distance fog. No component means no fog.
    struct FogComponent {
        bool enabled = true;
        float startDistance = 8.0f;
        float endDistance = 40.0f;
        std::array<float, 3> color{0.45f, 0.55f, 0.65f}; // Linear Rec.709.

        [[nodiscard]] FogSettings GetSettings() const noexcept {
            return {enabled, startDistance, endDistance, color};
        }
        [[nodiscard]] bool IsValid() const noexcept { return GetSettings().IsValid(); }
    };
}
