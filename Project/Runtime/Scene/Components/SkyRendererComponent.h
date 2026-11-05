#pragma once
#include "Foundation/Reflection/ReflectionMarkers.h"
NEXUS_REFLECT_COMPONENT(SkyRendererComponent, "SkyRenderer", "component.sky", enabled, intensity)
#include <cmath>

namespace NexusEngine {
    /** Opts this scene into sky rendering. Owns settings only, never GPU resources. */
    struct SkyRendererComponent {
        bool enabled = true;
        float intensity = 1.0f;
        [[nodiscard]] bool IsValid() const noexcept {
            return std::isfinite(intensity) && intensity >= 0;
        }
    };
}
