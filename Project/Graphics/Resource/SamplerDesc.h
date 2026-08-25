#pragma once

// c++
#include <cstdint>
#include <limits>

namespace NexusEngine {
	/** Texture縮小・拡大・mip補間へ共通適用する第一段階のfilter。 */
	enum class TextureFilter : uint8_t {
		Nearest,
		Linear
	};

	/** Texture座標が0～1を越えた場合のaddressing規則。 */
	enum class TextureAddressMode : uint8_t {
		Repeat,
		Mirror,
		Clamp
	};

	/*-----------------------------------------------------------------------------------------
	 * SamplerDesc
	 * - Bindless Sampler生成に必要なBackend非依存設定を保持する
	 * - Sampler DescriptorやShaderResourceRef自体は所有しない
	 *---------------------------------------------------------------------------------------*/
	struct SamplerDesc {
		TextureFilter filter = TextureFilter::Linear;
		TextureAddressMode addressU = TextureAddressMode::Repeat;
		TextureAddressMode addressV = TextureAddressMode::Repeat;
		TextureAddressMode addressW = TextureAddressMode::Repeat;
		float mipLodBias = 0.0f;
		float minLod = 0.0f;
		float maxLod = (std::numeric_limits<float>::max)();
	};
} // namespace NexusEngine
