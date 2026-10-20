#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace NexusEngine {
    enum class TextureColorSpace : uint8_t { Linear, Srgb };

    // Scene working space is linear Rec.709 / D65. sRGB is an input/output encoding.
    // These helpers transform RGB only; alpha, normals and material data are linear.
    namespace ColorSpace {
        [[nodiscard]] inline float SrgbToLinear(float value) noexcept {
            value = (std::max)(value, 0.0f);
            return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
        }
        [[nodiscard]] inline float LinearToSrgb(float value) noexcept {
            value = (std::max)(value, 0.0f);
            return value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
        }
        template<size_t N> [[nodiscard]] std::array<float, N> SrgbToLinear(std::array<float, N> value) noexcept {
            static_assert(N == 3 || N == 4);
            for(size_t i = 0; i < 3; ++i) value[i] = SrgbToLinear(value[i]);
            return value;
        }
        template<size_t N> [[nodiscard]] std::array<float, N> LinearToSrgb(std::array<float, N> value) noexcept {
            static_assert(N == 3 || N == 4);
            for(size_t i = 0; i < 3; ++i) value[i] = LinearToSrgb(value[i]);
            return value;
        }
    }
}
