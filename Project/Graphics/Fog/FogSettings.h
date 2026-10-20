#pragma once
#include <array>
#include <cmath>

namespace NexusEngine {
    struct FogSettings {
        bool enabled = false;
        float startDistance = 8.0f;
        float endDistance = 40.0f;
        std::array<float, 3> color {0.45f, 0.55f, 0.65f}; // Linear Rec.709 RGB.

        [[nodiscard]] bool IsValid() const noexcept {
            if(!std::isfinite(startDistance) || !std::isfinite(endDistance) ||
                startDistance < 0 || endDistance <= startDistance) return false;
            for(float value : color) if(!std::isfinite(value) || value < 0 || value > 65504) return false;
            return true;
        }
    };
}
