#pragma once

#include <cstdint>
#include <d3d12.h>

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * DescriptorHandle
	 * - Descriptor領域の位置、Heap種別、CPU/GPU Handleを利用側へ安全に渡す
	 * - Descriptor HeapまたはGPU Resourceの所有権は持たない
	 *---------------------------------------------------------------------------------------*/
	struct DescriptorHandle {
		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle {}; //< Descriptor書込用CPU Handle
		D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle {}; //< Shader-visible Heapでのみ有効
		uint32_t index = 0; //< Heap先頭からのIndex
		uint32_t count = 0; //< 所有する連続Descriptor数
		D3D12_DESCRIPTOR_HEAP_TYPE heapType = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; //< 所属Heap種別
		uint64_t allocatorId = 0; //< 誤ったAllocatorへの解放を検出する識別子

		[[nodiscard]] bool IsValid() const noexcept { return count > 0 && allocatorId != 0 && cpuHandle.ptr != 0; }
		[[nodiscard]] bool HasGpuHandle() const noexcept { return IsValid() && gpuHandle.ptr != 0; }
	};

} // namespace NexusEngine
