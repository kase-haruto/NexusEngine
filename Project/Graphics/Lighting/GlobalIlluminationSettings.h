#pragma once
#include <cmath>
#include <cstdint>
namespace NexusEngine {
    /*-----------------------------------------------------------------------------------------
     * GlobalIlluminationSettings
     * - Sceneから渡す拡散RTGI品質設定。GPU参照やDXR固有型は含めない
     *---------------------------------------------------------------------------------------*/
    struct GlobalIlluminationSettings {
        bool enabled = false; //< 既存Sceneは明示的に有効化する。Editorデモでは有効
        uint32_t samplesPerPixel = 8; //< 各pixelのcosine-weighted経路数、1～64
        uint32_t maxBounces = 2; //< 受光面から追跡する拡散rayの最大segment数、1～4
        float intensity = 1; //< 物理基準は1。比較用の間接拡散倍率
        /** \brief 外部設定による無限ループ・不正な放射値を防ぐ。 */
        [[nodiscard]] bool IsValid() const noexcept {
            return samplesPerPixel >= 1 && samplesPerPixel <= 64 && maxBounces >= 1 && maxBounces <= 4 &&
                std::isfinite(intensity) && intensity >= 0 && intensity <= 10;
        }
    };
}
