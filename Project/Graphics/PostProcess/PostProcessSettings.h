#pragma once
#include <array>
#include <cmath>
#include <cstdint>

namespace NexusEngine {
    enum class ToneMapper : uint32_t { LinearClamp = 0, AcesFitted = 1, PbrNeutral = 2 };
    // Scene-referred linear RGB. Neutral grading preserves radiometric ratios.
    struct PostProcessSettings {
        ToneMapper toneMapper = ToneMapper::PbrNeutral;
        float exposureEV = 0.0f;
        float bloomIntensity = 0.08f;
        float bloomThreshold = 1.0f;
        float bloomSoftKnee = 0.5f;
        float saturation = 1.0f;
        float contrast = 1.0f;
        float gamma = 1.0f;
        std::array<float, 3> gain {1, 1, 1};
        std::array<float, 3> offset {0, 0, 0};
        // Camera white balance multipliers in linear Rec.709 (D65 is neutral).
        std::array<float, 3> whiteBalance {1, 1, 1};
        bool bloomEnabled = true;
        bool filmicEnabled = true;
        bool antiAliasingEnabled = true;

        [[nodiscard]] bool IsValid() const noexcept {
            if(toneMapper != ToneMapper::LinearClamp && toneMapper != ToneMapper::AcesFitted && toneMapper != ToneMapper::PbrNeutral) return false;
            auto range = [](float v, float lo, float hi) { return std::isfinite(v) && v >= lo && v <= hi; };
            if(!range(exposureEV, -20, 20) || !range(bloomIntensity, 0, 10) ||
               !range(bloomThreshold, 0, 65504) || !range(bloomSoftKnee, 0, 1) ||
               !range(saturation, 0, 4) || !range(contrast, 0.01f, 4) || !range(gamma, 0.01f, 4)) return false;
            for(int i = 0; i < 3; ++i)
                if(!range(gain[i], 0, 16) || !range(offset[i], -16, 16) || !range(whiteBalance[i], 0.01f, 16)) return false;
            return true;
        }
        // Legacy filmicEnabled=false still requests linear clamp for existing callers.
        [[nodiscard]] ToneMapper GetEffectiveToneMapper() const noexcept {
            return filmicEnabled ? toneMapper : ToneMapper::LinearClamp;
        }
    };
}
