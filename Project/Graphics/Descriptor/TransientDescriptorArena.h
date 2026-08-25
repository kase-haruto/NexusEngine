#pragma once

// c++
#include <cstdint>
#include <vector>

// directx
#include <d3d12.h>

// engine
#include "DescriptorHandle.h"
#include "Foundation/Error/Result.h"

namespace NexusEngine {
	class DescriptorAllocator;

	/*-----------------------------------------------------------------------------------------
	 * TransientDescriptorArena
	 * - Frame中に生成する短寿命Resource Descriptorをbump allocationで払い出す
	 * - 各Frame領域は、そのFrameのFence完了後にだけGraphicsSystemからResetされる
	 * - 永続Assetの世代管理やSampler Descriptorは担当しない
	 *---------------------------------------------------------------------------------------*/
	class TransientDescriptorArena final {
	public:
		TransientDescriptorArena() noexcept = default;
		TransientDescriptorArena(const TransientDescriptorArena&) = delete;
		TransientDescriptorArena& operator=(const TransientDescriptorArena&) = delete;

		/**
		 * \brief Shader-visible Resource HeapからFrameごとの連続領域を予約する
		 * \param device CBV生成に使用する非所有Device
		 * \param allocator 領域の所有元となる非所有Allocator
		 * \param frameCount 同時進行するFrameContext数
		 * \param descriptorsPerFrame 各Frameが使用できる最大Descriptor数
		 */
		[[nodiscard]] Result<void> Initialize(
			ID3D12Device* device,
			DescriptorAllocator* allocator,
			uint32_t frameCount,
			uint32_t descriptorsPerFrame);

		/** \brief 予約した全Frame領域をAllocatorへ返却する。GPU idle後に呼ぶこと */
		void Shutdown() noexcept;

		/**
		 * \brief Fence完了済みFrameのbump cursorを先頭へ戻す
		 * \note GPUが同じFrame領域を参照中に呼んではならない
		 */
		void ResetFrame(uint32_t frameIndex) noexcept;

		/**
		 * \brief 現在Frameの一時領域へConstant Buffer Viewを生成する
		 * \return Root Descriptor Tableへ直接設定可能なGPU Handle
		 */
		[[nodiscard]] Result<D3D12_GPU_DESCRIPTOR_HANDLE> CreateConstantBufferView(
			uint32_t frameIndex,
			D3D12_GPU_VIRTUAL_ADDRESS bufferLocation,
			uint32_t sizeInBytes);

	private:
		struct FrameArena {
			DescriptorHandle allocation; //< 起動中占有するFrame専用の連続領域
			uint32_t cursor = 0; //< 次に書き込む領域内offset
		};

		ID3D12Device* device_ = nullptr; //< GraphicsSystem所有Deviceへの非所有参照
		DescriptorAllocator* allocator_ = nullptr; //< DescriptorManager所有Allocatorへの非所有参照
		std::vector<FrameArena> frames_; //< FrameContextと同じindexで管理するArena
		uint32_t descriptorsPerFrame_ = 0; //< Frame単位の固定上限
	};
} // namespace NexusEngine
