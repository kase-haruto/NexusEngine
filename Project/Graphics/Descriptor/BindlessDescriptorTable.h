#pragma once

#include <cstdint>
#include <vector>

#include <d3d12.h>

#include "DescriptorAllocator.h"
#include "Foundation/Error/Result.h"
#include "ShaderResourceRef.h"

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * BindlessDescriptorTable
	 * - Direct Heap Indexing用Resource/Sampler Heapと永続ShaderResourceRefを管理する
	 * - Textureロード、Material、GPU Resource本体は所有しない
	 *---------------------------------------------------------------------------------------*/
	class BindlessDescriptorTable final {
	public:
		/**
		 * \brief Direct Heap Indexing用のResource/Sampler Heapを初期化する
		 * \param device Heapとnull Descriptorの生成に使用する非所有Device
		 * \param resourceCapacity CBV/SRV/UAV Heapの固定slot数
		 * \param samplerCapacity Sampler Heapの固定slot数
		 * \return 両Heapとindex 0の予約に成功した場合は成功結果
		 */
		[[nodiscard]] Result<void> Initialize(ID3D12Device* device, uint32_t resourceCapacity, uint32_t samplerCapacity);
		/**
		 * \brief Heap、generation、retire queueを破棄する
		 * \note 呼び出し側が事前にCommandQueueのGPU完了を保証する
		 */
		void Shutdown() noexcept;

		/**
		 * \brief Resource HeapへSRVを生成する
		 * \param resource SRVが参照する非所有GPU Resource。null SRVではnullptrも許可する
		 * \param desc SRVの形式と範囲
		 * \return Shaderへindexを渡せるgeneration付き参照
		 */
		[[nodiscard]] Result<ShaderResourceRef> CreateShaderResourceView(ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC& desc);
		/**
		 * \brief Resource HeapへUAVを生成する
		 * \param resource UAV対象の非所有GPU Resource
		 * \param counterResource StructuredBuffer用の任意Counter Resource
		 * \param desc UAVの形式と範囲
		 * \return generation付きBindless参照
		 */
		[[nodiscard]] Result<ShaderResourceRef> CreateUnorderedAccessView(ID3D12Resource* resource, ID3D12Resource* counterResource, const D3D12_UNORDERED_ACCESS_VIEW_DESC& desc);
		/**
		 * \brief Resource HeapへCBVを生成する
		 * \param desc 256byte alignment済みGPU AddressとSize
		 * \return generation付きBindless参照
		 */
		[[nodiscard]] Result<ShaderResourceRef> CreateConstantBufferView(const D3D12_CONSTANT_BUFFER_VIEW_DESC& desc);
		/**
		 * \brief Sampler HeapへSampler Descriptorを生成する
		 * \param desc Filter、Address mode、LODなどのSampler設定
		 * \return Sampler Heapを指すgeneration付きBindless参照
		 */
		[[nodiscard]] Result<ShaderResourceRef> CreateSampler(const D3D12_SAMPLER_DESC& desc);

		/**
		 * \brief Descriptorを指定Fence完了後に再利用するretire queueへ移す
		 * \note retireFenceValueはこの参照を使用した最後のCommand投入後のFence値
		 */
		[[nodiscard]] Result<void> Retire(ShaderResourceRef reference, uint64_t retireFenceValue);
		/**
		 * \brief 完了Fenceまでのretire済みslotをAllocatorへ返却する
		 * \param completedFenceValue CommandQueueが報告する現在の完了値
		 * \note 回収時にgenerationを更新し、古いCPU Handleを無効化する
		 */
		void CollectGarbage(uint64_t completedFenceValue) noexcept;
		/**
		 * \brief Heap分類、範囲、割当状態、generationを含めて参照を検証する
		 * \param reference 検証対象
		 * \return 現在Shader Resourceとして利用可能な場合true
		 */
		[[nodiscard]] bool IsValid(ShaderResourceRef reference) const noexcept;
		/**
		 * \brief ResourceDescriptorHeapとSamplerDescriptorHeapをCommandListへ設定する
		 * \param commandList Heapを使用する非所有CommandList
		 */
		void Bind(ID3D12GraphicsCommandList* commandList) const noexcept;

	private:
		struct SlotState {
			uint32_t generation = 1; //< 再利用ごとに更新する世代番号
			bool active = false;     //< 新しい描画へ公開中か
			bool retiring = false;   //< GPU完了待ちでAllocatorへ未返却か
		};
		struct RetiredSlot {
			ShaderResourceRef reference; //< 解放要求時のindexとgeneration
			uint64_t fenceValue = 0;     //< 再利用可能になるFence値
		};

		/** \brief Heap分類に対応するAllocatorから一slot確保し世代付き参照を生成する */
		[[nodiscard]] Result<ShaderResourceRef> Allocate(ShaderResourceClass resourceClass, DescriptorAllocation& allocation);
		[[nodiscard]] std::vector<SlotState>& GetSlots(ShaderResourceClass resourceClass) noexcept;
		[[nodiscard]] DescriptorAllocator& GetAllocator(ShaderResourceClass resourceClass) noexcept;

		ID3D12Device* device_ = nullptr; //< GraphicsDeviceが所有する非所有Device
		DescriptorAllocator resources_; //< CBV/SRV/UAV Bindless Heap
		DescriptorAllocator samplers_;  //< Sampler Bindless Heap
		std::vector<SlotState> resourceSlots_; //< Resource slot generationと状態
		std::vector<SlotState> samplerSlots_;  //< Sampler slot generationと状態
		std::vector<RetiredSlot> retired_;     //< Fence完了待ちの遅延解放slot
	};

} // namespace NexusEngine
