#pragma once

// c++
#include <cstdint>
#include <span>
#include <vector>

// engine
#include "Foundation/Error/Result.h"

namespace NexusEngine {
	struct DecodedImage {
		uint32_t width = 0;
		uint32_t height = 0;
		std::vector<uint8_t> rgbaPixels;
	};

	/*-----------------------------------------------------------------------------------------
	 * WicImageDecoder
	 * - PNG/JPEG等のencoded byte列をRGBA8へ変換するWindows backend helper
	 * - GPU Resource生成、Descriptor、File I/Oは担当しない
	 *---------------------------------------------------------------------------------------*/
	class WicImageDecoder final {
	public:
		/** \brief encoded画像を密配置RGBA8 pixelへdecodeする */
		[[nodiscard]] Result<DecodedImage> Decode(std::span<const uint8_t> encodedData) const;
	};
} // namespace NexusEngine
