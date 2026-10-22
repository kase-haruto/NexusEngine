#pragma once
#include <array>
#include <cmath>
#include <cstdint>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * MaterialOverride
 * - 共有モデルを変更せず、描画Instanceの一つの材質Slotへ適用するCPU設定
 * - Runtime保存とRaster/DXRが同じ値を共有し、GPU資源は所有しない
 *---------------------------------------------------------------------------------------*/
struct MaterialOverride {
    bool enabled = false; //< falseではインポートした全パラメータを維持する
    int slot = 0; //< Sourceモデル内の材質Slot。統合モデルのindexとは独立
    std::array<float, 4> baseColor {1, 1, 1, 1}; //< 線形RGBA
    float metallic = 0; //< 金属率、0〜1
    float roughness = 1; //< 粗さ、0〜1
    float normalScale = 1; //< 継承する法線Mapの強度
    std::array<float, 3> emissive {}; //< 線形HDR発光色
    float occlusionStrength = 1; //< 継承するAO Mapの強度、0〜1
    /** \brief Reflectionおよび保存時に同じ有限値・範囲検証を使う */
    bool IsValid() const {
        if(slot < 0 || !std::isfinite(metallic) || metallic < 0 || metallic > 1 ||
            !std::isfinite(roughness) || roughness < 0 || roughness > 1 ||
            !std::isfinite(normalScale) ||
            !std::isfinite(occlusionStrength) || occlusionStrength < 0 || occlusionStrength > 1) return false;
        for(float f : baseColor) if(!std::isfinite(f) || f < 0 || f > 1) return false;
        for(float f : emissive) if(!std::isfinite(f) || f < 0) return false;
        return true;
    }
    /** \brief Source Slotと統合描画Slotをoffsetで対応付ける */
    bool AppliesTo(uint32_t resolvedSlot, uint32_t materialBegin) const {
        return enabled && resolvedSlot >= materialBegin && resolvedSlot - materialBegin == static_cast<uint32_t>(slot);
    }
    /** \brief RasterとDXRのCPU描画値へ同じOverrideを適用する */
    void Apply(std::array<float, 4>& color, std::array<float, 4>& pbr, std::array<float, 4>& emission) const {
        color = baseColor; pbr = {metallic, roughness, normalScale, 0};
        emission = {emissive[0], emissive[1], emissive[2], occlusionStrength};
    }
};
}
