#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
namespace NexusEngine {
    /** HLSLのデバッグモードと同順序。 */
    enum class LightingDebugMode : uint32_t {
        Final, Direct, Indirect, DiffuseIBL, SpecularIBL, BaseColor, Metallic, Roughness, Normal, AO, Emissive, DiffuseGI
    };
    /*-----------------------------------------------------------------------------------------
     * EnvironmentLightingData
     * - Sceneから渡す環境設定。GPU参照・画像・Descriptorを所有しない
     *---------------------------------------------------------------------------------------*/
    struct EnvironmentLightingData {
        std::filesystem::path imagePath; //< Radiance RGBE .hdr、線形Rec.709、Y up
        bool enabled = true;
        float intensity = 1;
        float rotation = 0; //< Y軸yaw radians。lookup時に逆回転
        uint64_t revision = 0; //< 同一pathの再読み込み要求
        LightingDebugMode debugMode = LightingDebugMode::Final;
        bool legacyHalfLambert = false;
    };
    /** Graphics内部の転送値。Scene/Runtimeは保持しない。HLSLと48byte一致。 */
    struct EnvironmentDrawData {
        std::array<uint32_t, 4> textures {}; //< radiance/irradiance/prefilter/LUT
        std::array<float, 4> parameters {0, 0, 1, 0}; //< intensity/sin(yaw)/cos(yaw)/maxMip
        std::array<uint32_t, 4> options {}; //< sampler/enabled/debug/legacy
    };
    static_assert(sizeof(EnvironmentDrawData) == 48);
}
