#pragma once

#include <cstdint>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "Foundation/Error/Result.h"
#include "DescriptorHandle.h"

namespace NexusEngine {

	using DescriptorAllocation = DescriptorHandle;

	/*-----------------------------------------------------------------------------------------
	 * DescriptorAllocator
	 * - 単一Heap種別の生成、連続Descriptor確保・解放、Handle計算を担当する
	 * - Resource、Material、Pipelineの所有やGPU使用完了判定は担当しない
	 *---------------------------------------------------------------------------------------*/
	class DescriptorAllocator final {
	public:
		/**
		 * \brief 指定Heap種別と可視性で固定容量Allocatorを初期化する
		 * \param device Heap生成とHandle増分取得に使用する非所有Device
		 * \param type Descriptor Heap種別
		 * \param capacity Heap内のDescriptor数
		 * \param shaderVisible GPUから参照可能にする場合true
		 * \return 初期化結果
		 */
		[[nodiscard]] Result<void> Initialize(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t capacity, bool shaderVisible);
		/** \brief Heapと割当状態を破棄する */
		void Shutdown() noexcept;
		/**
		 * \brief Heapから連続Descriptor領域を確保する
		 * \param count 必要な連続Descriptor数
		 * \return IndexとCPU/GPU Handleを持つAllocation
		 */
		[[nodiscard]] Result<DescriptorAllocation> Allocate(uint32_t count = 1);
		/**
		 * \brief 確保済みDescriptor領域を再利用可能にする
		 * \param allocation このAllocatorから取得したAllocation
		 * \return 二重解放または範囲外の場合はエラー
		 * \note GPU完了はFrameまたはResource所有者が事前に保証する
		 */
		[[nodiscard]] Result<void> Free(const DescriptorAllocation& allocation);
		/** \brief CommandListへBindするHeapを非所有参照として取得する */
		[[nodiscard]] ID3D12DescriptorHeap* GetHeap() const noexcept;
		[[nodiscard]] bool IsShaderVisible() const noexcept { return shaderVisible_; }
		[[nodiscard]] D3D12_DESCRIPTOR_HEAP_TYPE GetHeapType() const noexcept { return heapType_; }
		[[nodiscard]] uint32_t GetCapacity() const noexcept { return static_cast<uint32_t>(allocated_.size()); }
		[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle(const DescriptorHandle& allocation, uint32_t offset = 0) const noexcept;
		[[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(const DescriptorHandle& allocation, uint32_t offset = 0) const noexcept;

	private:
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_; //< Descriptor Heap所有者
		std::vector<bool> allocated_; //< 各Indexの割当状態
		uint32_t descriptorSize_ = 0; //< Handle計算用Device依存増分
		bool shaderVisible_ = false; //< GPU Handleの有効性
		D3D12_DESCRIPTOR_HEAP_TYPE heapType_ = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; //< Heap種別
		uint64_t allocatorId_ = 0; //< 別Heap由来HandleのFreeを拒否する識別子
	};

} // namespace NexusEngine
