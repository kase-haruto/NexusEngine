#pragma once
#include <array>
#include <cmath>

namespace NexusEngine {
    struct FogSettings {
        bool enabled = false;
        float startDistance = 8.0f;
        float endDistance = 40.0f;
        std::array<float, 3> color {0.45f, 0.55f, 0.65f}; // Linear Rec.709 RGB.
        bool exponentialHeight = false;
        float density = .01f, heightFalloff = .2f, height = 0;
        float maxOpacity = 1, cutoffDistance = 0;

        [[nodiscard]] bool IsValid() const noexcept {
            if(!std::isfinite(startDistance) || !std::isfinite(endDistance) ||
                startDistance < 0 || (!exponentialHeight && endDistance <= startDistance)) return false;
            for(float value : color) if(!std::isfinite(value) || value < 0 || value > 65504) return false;
            return std::isfinite(density) && density >= 0 && density <= 10 &&
                std::isfinite(heightFalloff) && heightFalloff >= 0 && heightFalloff <= 10 &&
                std::isfinite(height) && std::isfinite(maxOpacity) && maxOpacity >= 0 && maxOpacity <= 1 &&
                std::isfinite(cutoffDistance) && cutoffDistance >= 0;
        }
    };
}
