#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace NexusEngine {
    // Scene-linear, Y-up ground observer model. Distances in the fog API are metres.
    struct AtmosphereDrawData {
        std::array<float, 4> sun {0, 1, 0, 20};
        std::array<float, 4> scattering {1, 1, .8f, .002f}; // Rayleigh, Mie, anisotropy, night radiance
        std::array<float, 4> ground {.08f, .09f, .1f, 0}; // albedo, enabled
    };
    static_assert(sizeof(AtmosphereDrawData) == 48);

    struct AtmosphereSettings {
        bool enabled = false;
        float timeOfDay = 12; // 0..24; sunrise 6, noon 12, sunset 18
        float sunAzimuth = 0; // degrees, rotates the solar orbit about world Y
        float sunIntensity = 4;
        float skyIntensity = 1;
        float rayleighScale = 1;
        float mieScale = 1;
        float mieAnisotropy = .8f;
        float nightIntensity = .002f;
        bool driveSun = true;
        std::array<float, 3> sunDirectionOverride {}; // extraction-only, never serialized

        [[nodiscard]] bool IsValid() const noexcept {
            const auto range = [](float v, float lo, float hi) { return std::isfinite(v) && v >= lo && v <= hi; };
            return range(timeOfDay, 0, 24) && range(sunAzimuth, -360, 360) &&
                range(sunIntensity, 0, 1000) && range(skyIntensity, 0, 100) &&
                range(rayleighScale, 0, 10) && range(mieScale, 0, 10) &&
                range(mieAnisotropy, 0, .95f) && range(nightIntensity, 0, 1);
        }
        [[nodiscard]] std::array<float, 3> SunDirection() const noexcept {
            const auto& d = sunDirectionOverride;
            const float length = std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
            if(length > .001f && std::isfinite(length)) return {d[0]/length, d[1]/length, d[2]/length};
            constexpr float pi = 3.14159265358979323846f;
            const float angle = (timeOfDay - 6) * pi / 12, azimuth = sunAzimuth * pi / 180;
            return {std::cos(angle) * std::cos(azimuth), std::sin(angle), std::cos(angle) * std::sin(azimuth)};
        }
        [[nodiscard]] std::array<float, 3> SunTransmittance() const noexcept {
            const float elevation = SunDirection()[1];
            if(elevation <= -.02f) return {};
            const float airMass = 1 / ((std::max)(elevation, 0.0f) + .025f);
            std::array<float, 3> result;
            constexpr float rayleigh[3] {.005802f, .013558f, .0331f};
            for(size_t i = 0; i < 3; ++i)
                result[i] = std::exp(-(rayleigh[i] * 8 * rayleighScale + .00444f * 1.2f * mieScale) * airMass) *
                    std::clamp((elevation + .02f) / .04f, 0.0f, 1.0f);
            return result;
        }
        [[nodiscard]] AtmosphereDrawData GetDrawData() const noexcept {
            const auto direction = SunDirection();
            return {{direction[0], direction[1], direction[2], 20 * skyIntensity},
                {rayleighScale, mieScale, mieAnisotropy, nightIntensity * skyIntensity},
                {.08f, .09f, .1f, enabled && IsValid() ? 1.0f : 0.0f}};
        }
        // Same approximation as Common/Atmosphere.hlsli, without the direct solar disk.
        [[nodiscard]] std::array<float, 3> Radiance(const std::array<float, 3>& direction) const noexcept {
            constexpr float pi = 3.14159265358979323846f;
            const auto sun = SunDirection();
            const auto smooth = [](float lo, float hi, float v) {
                const float t = std::clamp((v-lo)/(hi-lo), 0.0f, 1.0f); return t*t*(3-2*t);
            };
            const float daylight = smooth(-.12f, .02f, sun[1]);
            const float sunMass = 1 / ((std::max)(sun[1], 0.0f) + .025f);
            const float viewMass = 1 / ((std::max)(direction[1], 0.0f) + .08f);
            float mu = 0;
            for(size_t i = 0; i < 3; ++i) mu += direction[i]*sun[i];
            mu = std::clamp(mu, -1.0f, 1.0f);
            const float g = mieAnisotropy;
            const float phaseR = 3*(1+mu*mu)/(16*pi);
            const float phaseM = (1-g*g)/(4*pi*std::pow((std::max)(1+g*g-2*g*mu, .001f), 1.5f));
            constexpr float beta[3] {.005802f, .013558f, .0331f};
            constexpr float night[3] {.25f, .4f, 1}, albedo[3] {.08f, .09f, .1f};
            std::array<float, 3> result;
            for(size_t i = 0; i < 3; ++i) {
                const float r = beta[i]*rayleighScale*8, m = .00444f*mieScale*1.2f;
                const float optical = r+m, sunT = std::exp(-optical*sunMass);
                const float source = (r*phaseR + .003996f*mieScale*1.2f*phaseM)/(std::max)(optical, .000001f);
                float color = 20*skyIntensity*daylight*sunT*source*(1-std::exp(-optical*viewMass));
                color += nightIntensity*skyIntensity*night[i]*(.4f+.6f*std::clamp(direction[1], 0.0f, 1.0f));
                if(direction[1] < 0) {
                    const float ground = albedo[i]*(20*skyIntensity*(std::max)(sun[1], 0.0f)*sunT/pi+nightIntensity*skyIntensity);
                    const float blend = smooth(0, .08f, -direction[1]); color = color*(1-blend)+ground*blend;
                }
                result[i] = (std::max)(color, 0.0f);
            }
            return result;
        }
    };
    struct SkyLightSettings {
        bool enabled = true;
        bool useAtmosphere = true; // false uses the Level HDR environment
        float intensity = 1;
        [[nodiscard]] bool IsValid() const noexcept { return std::isfinite(intensity) && intensity >= 0 && intensity <= 100; }
    };
}
