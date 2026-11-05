#pragma once
#include <array>
#include <cmath>
namespace NexusEngine {
    struct GridSettings {
        bool enabled = false;
        float spacing = 1, height = 0, fadeDistance = 100000;
        std::array<float, 4> color {0.35f, 0.35f, 0.35f, 0.65f}; // Linear RGB + opacity.
        [[nodiscard]] bool IsValid() const noexcept {
            if(!std::isfinite(spacing) || spacing < 0.001f || !std::isfinite(height) ||
               !std::isfinite(fadeDistance) || fadeDistance <= spacing || fadeDistance > 100000) return false;
            for(float value : color) if(!std::isfinite(value) || value < 0 || value > 1) return false;
            return true;
        }
    };
}
