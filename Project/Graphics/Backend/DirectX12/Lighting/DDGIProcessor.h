#pragma once
#include "Foundation/Error/Result.h"
#include "Graphics/Descriptor/ShaderResourceRef.h"
#include "Graphics/Lighting/DDGISettings.h"
#include "Graphics/Resource/ConstantBuffer.h"
#include <d3d12.h>
#include <filesystem>
#include <wrl/client.h>
namespace NexusEngine {
	class GraphicsResourceFactory;
	class CommandQueue;
	class BindlessDescriptorTable;
	/*-----------------------------------------------------------------------------------------
	 * DDGIProcessor
	 * - プローブ履歴、レイ作業領域、照度・距離のCompute更新と寿命を管理する
	 * - AS・材質・光源の所有とレイ追跡は既存OpticalBackendへ委譲する
	 *---------------------------------------------------------------------------------------*/
	class DDGIProcessor final {
	public:
		/** \brief Backend共有依存へ接続する。Device・Queue・Heapは非所有。 */
		[[nodiscard]] Result<void> Initialize(ID3D12Device*, CommandQueue*, BindlessDescriptorTable*, GraphicsResourceFactory&, const std::filesystem::path&);
		/** \brief 配置・品質変更時だけ資源を再構成し、必要なら履歴を破棄する。 */
		[[nodiscard]] Result<void> Prepare(const DDGISettings&);
		/** \brief レイ追跡開始前の履歴・状態を保存し、必要なBarrierを記録する。 */
		void Begin(ID3D12GraphicsCommandList*);
		/** \brief DXR結果をComputeで次の履歴へ積分し、全Shaderから参照可能にする。 */
		[[nodiscard]] Result<void> Update(ID3D12GraphicsCommandList*, uint32_t frame);
		/** \brief 未提出記録は履歴番号・Resource状態・更新番号を復元する。 */
		void CompleteRecording(bool submitted) noexcept;
		/** \brief 現在の履歴の描画値。強度1を渡すと多重反射用の物理量となる。 */
		[[nodiscard]] DDGIDrawData GetDrawData(float intensity = 1) const noexcept;
		/** \brief 履歴の初期化または継続更新が必要かを返す。 */
		[[nodiscard]] bool NeedsUpdate() const noexcept { return !settings_.freeze || !ready_; }
		/** \brief プローブ追跡先のBindless UAV番号をBackendへ渡す。 */
		[[nodiscard]] uint32_t RayUav() const noexcept { return rayUav_.index; }
		/** \brief 固定方向なら0、方向を回転する場合は更新番号を返す。 */
		[[nodiscard]] uint32_t Phase() const noexcept { return settings_.rotateRays ? frameNumber_ : 0; }
		/** \brief 現在の履歴へ記録した更新回数を返す。未提出時は復元される。 */
		[[nodiscard]] uint32_t FrameCount() const noexcept { return frameNumber_; }
		/** \brief GPU idle後に全資源・Descriptor・PSOを解放する。 */
		void Shutdown() noexcept;

	private:
		using Resource = Microsoft::WRL::ComPtr<ID3D12Resource>;
		struct History {
			std::array<Resource, 3>			 textures;
			std::array<ShaderResourceRef, 3> srv{}, uav{};
			D3D12_RESOURCE_STATES			 state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		};
		struct UpdateConstants {
			DDGIDrawData			previous;
			std::array<uint32_t, 4> outputs{};
			std::array<float, 4>	filter{};
			std::array<uint32_t, 4> options{};
		};
		static_assert(sizeof(UpdateConstants) == 128);
		[[nodiscard]] Result<void>					CreatePipeline();
		void										Release() noexcept;
		ID3D12Device*								device_ = nullptr;
		CommandQueue*								queue_	= nullptr;
		BindlessDescriptorTable*					table_	= nullptr;
		std::filesystem::path						shaders_;
		std::array<History, 2>						history_;
		Resource									rays_;
		ShaderResourceRef							raySrv_, rayUav_, sampler_;
		Microsoft::WRL::ComPtr<ID3D12RootSignature> root_;
		Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;
		ConstantBuffer								constants_;
		DDGISettings								settings_;
		uint32_t									read_ = 0, frameNumber_ = 0;
		bool										ready_ = false, recording_ = false, allocated_ = false;
		D3D12_RESOURCE_STATES						rayState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		std::array<D3D12_RESOURCE_STATES, 3>		initialStates_{};
		uint32_t									initialRead_ = 0, initialFrame_ = 0;
		bool										initialReady_ = false;
	};
} // namespace NexusEngine
