#pragma once

#include "DescriptorAllocator.h"

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * DescriptorManager
	 * - Renderer全体で共有するDirectX 12の4種Descriptor Heapを所有する
	 * - Descriptor領域だけを管理し、GPU ResourceやViewの内容は所有しない
	 *---------------------------------------------------------------------------------------*/
	class DescriptorManager final {
	public:
		[[nodiscard]] Result<void> Initialize(
			ID3D12Device* device,
			uint32_t resourceCapacity,
			uint32_t rtvCapacity,
			uint32_t dsvCapacity,
			uint32_t samplerCapacity);
		void Shutdown() noexcept;

		[[nodiscard]] DescriptorAllocator& Resources() noexcept { return resources_; }
		[[nodiscard]] DescriptorAllocator& RenderTargets() noexcept { return renderTargets_; }
		[[nodiscard]] DescriptorAllocator& DepthStencils() noexcept { return depthStencils_; }
		[[nodiscard]] DescriptorAllocator& Samplers() noexcept { return samplers_; }
		[[nodiscard]] const DescriptorAllocator& Resources() const noexcept { return resources_; }
		[[nodiscard]] const DescriptorAllocator& Samplers() const noexcept { return samplers_; }

		/** \brief ResourceとSamplerのshader-visible HeapをCommandListへ設定する */
		void BindShaderVisibleHeaps(ID3D12GraphicsCommandList* commandList) const noexcept;

	private:
		DescriptorAllocator resources_; //< CBV/SRV/UAV shader-visible Heap
		DescriptorAllocator renderTargets_; //< RTV CPU-only Heap
		DescriptorAllocator depthStencils_; //< DSV CPU-only Heap
		DescriptorAllocator samplers_; //< Sampler shader-visible Heap
	};

} // namespace NexusEngine
