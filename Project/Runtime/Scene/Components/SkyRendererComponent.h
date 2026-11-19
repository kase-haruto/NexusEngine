#pragma once
#include "Foundation/Reflection/ReflectionMarkers.h"
NEXUS_REFLECT_COMPONENT(SkyRendererComponent, "SkyRenderer", "component.sky", enabled, intensity)
NEXUS_COMPONENT_EDITOR(SkyRendererComponent, "Rendering", "componentBrowser.description.sky", "details.Sky")
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
