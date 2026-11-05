#pragma once
#include <array>
#include <cmath>
#include <filesystem>
#include <string>

namespace NexusEngine {
    // Display-space effect, after tone mapping/sRGB encoding and before FXAA/UI.
    // t0: previous colour, s0: linear clamp, b0: four float4 parameters.
    struct ScreenEffect {
        std::filesystem::path shaderPath; // Absolute, or relative to GraphicsSystemDesc::shaderDirectory.
        std::wstring entryPoint = L"PSMain";
        std::array<float, 16> parameters {};
        bool enabled = true;
        bool operator==(const ScreenEffect&) const = default;
        [[nodiscard]] bool IsValid() const noexcept {
            if(shaderPath.empty() || entryPoint.empty()) return false;
            for(float value : parameters) if(!std::isfinite(value)) return false;
            return true;
        }
    };
}
