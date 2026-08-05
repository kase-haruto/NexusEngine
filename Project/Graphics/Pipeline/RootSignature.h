#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include "Foundation/Error/Result.h"
#include "PipelineLayout.h"

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * RootSignature
	 * - PipelineLayoutを共通Descriptor Table規則へ変換してRootSignatureを所有する
	 *---------------------------------------------------------------------------------------*/
	class RootSignature final {
	public:
		/**
		 * \brief 統合済みBindingから最大2個のDescriptor Tableを生成する
		 * \param device RootSignature生成に使う非所有Device
		 * \param layout Shader Stage統合済みPipeline Layout
		 * \return Serializationおよび生成結果
		 */
		[[nodiscard]] Result<void> Initialize(ID3D12Device* device, const PipelineLayout& layout);
		/** \brief PSOおよびCommandList設定用RootSignatureを取得する */
		[[nodiscard]] ID3D12RootSignature* GetNative() const noexcept;
		/** \brief CBV/SRV/UAV Tableが存在するか取得する */
		[[nodiscard]] bool HasResourceTable() const noexcept;
		/** \brief Sampler Tableが存在するか取得する */
		[[nodiscard]] bool HasSamplerTable() const noexcept;
		/**
		 * \brief 内部Root parameter番号を使用してDescriptor TableをBindする
		 * \note 呼び出し側へRoot parameter番号を公開しない
		 */
		void BindDescriptorTables(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE resources, D3D12_GPU_DESCRIPTOR_HANDLE samplers) const noexcept;

	private:
		Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
		uint32_t resourceRootIndex_ = UINT32_MAX;
		uint32_t samplerRootIndex_ = UINT32_MAX;
	};

} // namespace NexusEngine
