#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>
#include "Foundation/Error/Result.h"
namespace NexusEngine {
    /** CPU画像。RGBEを線形floatへ展開し、GPU resource生成とは分離する。 */
    struct HdrImage {
        uint32_t width = 0, height = 0;
        std::vector<std::array<float, 4>> pixels;
    };
    /** \brief Radiance RGBE (-Y +X) のraw/RLE .hdrを読み込む。色変換・露出補正は行わない。 */
    [[nodiscard]] Result<HdrImage> LoadHdrImage(const std::filesystem::path& path);
}
