#pragma once

#include <cstdint>

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * ShaderResourceClass
	 * - 参照先が属するDirectX 12のShader-visible Heap分類を表す
	 *---------------------------------------------------------------------------------------*/
	enum class ShaderResourceClass : uint8_t {
		Resource, //< CBV、SRV、UAVが共有するResource Descriptor Heap
		Sampler   //< Sampler専用Descriptor Heap
	};

	/*-----------------------------------------------------------------------------------------
	 * ShaderResourceRef
	 * - Bindless Descriptorを参照するDX非依存の軽量Handle
	 * - Shaderへはindexだけを渡し、generationはCPU側の古い参照検出に使用する
	 *---------------------------------------------------------------------------------------*/
	struct ShaderResourceRef {
		uint32_t index = 0;           //< Shader-visible Heap内のIndex。0はnull Descriptor
		uint32_t generation = 0;      //< Slot再利用時に更新するCPU検証値
		ShaderResourceClass resourceClass = ShaderResourceClass::Resource; //< Resource/Sampler Heap分類

		/**
		 * \brief null参照ではないかを軽量に判定する
		 * \return indexとgenerationが予約値でない場合true
		 * \note 現在のslot generationとの一致はBindlessDescriptorTable::IsValidで確認する
		 */
		[[nodiscard]] bool IsValid() const noexcept { return index != 0 && generation != 0; }
	};

} // namespace NexusEngine
