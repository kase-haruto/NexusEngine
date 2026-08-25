#pragma once

// c++
#include <array>
#include <cstdint>

namespace NexusEngine {
	/*-----------------------------------------------------------------------------------------
	 * MaterialDrawData
	 * - 1 DrawのMaterial parameterとしてGPU Constant Bufferへ転送する値だけを保持する
	 * - Descriptor HandleやResource所有権を持たず、Bindless Heap indexだけをShaderへ渡す
	 *---------------------------------------------------------------------------------------*/
	struct MaterialDrawData {
		uint32_t textureIndex = 0; //< ResourceDescriptorHeap上のTexture index。0はnull Texture
		uint32_t samplerIndex = 0; //< SamplerDescriptorHeap上のSampler index。0はnull Sampler
		std::array<float, 2> padding = {}; //< HLSL cbufferの16byte register境界へ合わせる明示padding
		std::array<float, 4> tint = { 1.0f, 1.0f, 1.0f, 1.0f }; //< Textureと頂点色へ乗算するRGBA
	};

	static_assert(sizeof(MaterialDrawData) == 32);
} // namespace NexusEngine
