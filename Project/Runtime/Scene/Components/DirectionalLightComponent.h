#pragma once
#include "Graphics/Shadow/ShadowTypes.h"
#include <cmath>

namespace NexusEngine {
    // Local surface-to-light direction; entity world transform controls orientation.
    // The renderer supports one enabled directional light, shared with shadow passes.
    struct DirectionalLightComponent {
        DirectionalLightData light;
        bool enabled = true;
        [[nodiscard]] bool IsValid() const noexcept {
            float lengthSquared = 0;
            for(float value : light.direction) {
                if(!std::isfinite(value)) return false;
                lengthSquared += value * value;
            }
            for(float value : light.color) if(!std::isfinite(value) || value < 0) return false;
            return std::isfinite(lengthSquared) && lengthSquared > 1e-12f &&
                std::isfinite(light.intensity) && light.intensity >= 0 &&
                std::isfinite(light.angularRadius) && light.angularRadius >= 0 && light.angularRadius <= 1.5f;
        }
    };
}
