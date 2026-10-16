#pragma once

// c++
#include <cstdint>
#include <span>

// engine
#include "Foundation/Error/Result.h"
#include "TextureResource.h"

struct ID3D12Device;

namespace NexusEngine {
	class CommandQueue;

	/*-----------------------------------------------------------------------------------------
	 * TextureUploader
	 * - CPU pixel列をUpload staging経由でDefault Heap Textureへ転送する
	 * - 既定で1x1までのmipを生成する。sRGBのRGBは線形化して平均し、alpha/dataは線形で扱う
	 * - 一時Command資源とUpload ResourceをFence完了まで所有する
	 * - File decode、Texture cache、Descriptor生成は担当しない
	 *---------------------------------------------------------------------------------------*/
	class TextureUploader final {
	public:
		/**
		 * \brief tightly packedなRGBA pixel列を同期uploadする
		 * \param device Texture、Upload Buffer、Command資源を生成する非所有Device
		 * \param commandQueue 転送Commandを実行しFence待機する非所有Direct Queue
		 * \param texture 完成したDefault Heap Textureの所有先
		 * \param desc Texture寸法とformat
		 * \param pixels mip 0のRGBA pixel列（RGBA8は4、RGBA32Floatは16 byte/pixel）
		 */
		[[nodiscard]] Result<void> Upload(
			ID3D12Device* device,
			CommandQueue* commandQueue,
			TextureResource& texture,
			const TextureDesc& desc,
			std::span<const uint8_t> pixels) const;
	};
} // namespace NexusEngine
