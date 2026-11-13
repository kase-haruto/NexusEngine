#pragma once
#include "Foundation/Reflection/ReflectionMarkers.h"
#include "Graphics/Fog/FogSettings.h"
NEXUS_REFLECT_COMPONENT(ExponentialHeightFogComponent, "ExponentialHeightFog", "component.heightFog", enabled, density, heightFalloff, heightOffset, startDistance, maxOpacity, cutoffDistance, color, atmosphereContribution)
namespace NexusEngine {
    struct ExponentialHeightFogComponent {
        bool enabled = true;
        float density = .01f;
        float heightFalloff = .2f;
        float heightOffset = 0;
        float startDistance = 0;
        float maxOpacity = 1;
        float cutoffDistance = 0; // zero disables cutoff
        std::array<float, 3> color {.45f, .55f, .65f};
        float atmosphereContribution = 1;
        [[nodiscard]] FogSettings GetSettings(float worldHeight = 0) const noexcept {
            FogSettings result;
            result.enabled = enabled; result.exponentialHeight = true;
            result.density = density; result.heightFalloff = heightFalloff;
            result.height = worldHeight + heightOffset; result.startDistance = startDistance;
            result.maxOpacity = maxOpacity; result.cutoffDistance = cutoffDistance;
            result.color = color;
            return result;
        }
        [[nodiscard]] bool IsValid() const noexcept {
            return GetSettings().IsValid() && std::isfinite(atmosphereContribution) && atmosphereContribution >= 0 && atmosphereContribution <= 10;
        }
    };
}
