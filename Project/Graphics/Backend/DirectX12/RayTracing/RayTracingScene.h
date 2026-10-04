#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

#include "Foundation/Error/Result.h"
#include "Graphics/Shadow/ShadowTypes.h"

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * RayTracingScene
	 * - DX12 Backend内部でBLASキャッシュとFrame別TLAS/Instance/Scratchを所有する
	 * - Geometry世代変更時だけBLASを生成し、Transform変更はTLAS updateへ分離する
	 * - Scene、Entity、GameObject、Lighting、Shadow方式選択は管理しない
	 * - Frame再利用前のFence待機と終了時GPU idleは接続元GraphicsSystemの責務
	 *---------------------------------------------------------------------------------------*/
	class RayTracingScene final {
	public:
		/** \brief AS生成Deviceと既存Frame slot数を接続する。GPU命令はまだ発行しない */
		[[nodiscard]] Result<void> Initialize(ID3D12Device* device, uint32_t frameCount);
		/**
		 * \brief GeometryからBLASを取得し、描画InstanceのTLASを記録する
		 * \param commands 現在FrameのDirect Command List4
		 * \param frameIndex Fence待機済みのFrame slot
		 * \param instances Sceneに依存しない描画Instance入力
		 * \param hitGroupOffsets Instance別Material ShaderTable offset。空なら共通HitGroup
		 * \return Geometry検証、Resource確保、AS記録の結果
		 * 同一snapshotは再Buildしない。ResourceとDescriptorはGPUが参照する間保持する。
		 */
		[[nodiscard]] Result<void> Update(ID3D12GraphicsCommandList4* commands, uint32_t frameIndex,
			std::span<const ShadowDrawItem> instances, std::span<const uint32_t> hitGroupOffsets = {});
		/** \brief GPU完了後のみ呼ぶ。BLAS/TLAS/Input/Scratchの全所有権を解放する */
		void Shutdown() noexcept;
		/** \brief 現在slotのTLAS GPU VA。空Sceneなら0。上位Rendererには公開しない */
		[[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS GetTlasAddress(uint32_t frameIndex) const noexcept;
		[[nodiscard]] ShadowStatistics GetStatistics() const noexcept;
		/** \brief 未投入Command Listを破棄した場合、記録したBuildを有効扱いしない */
		void InvalidateRecording(uint32_t frameIndex) noexcept;

	private:
		struct Blas {
			Microsoft::WRL::ComPtr<ID3D12Resource> vertices; //< Mesh解放後もGPU Build入力を保つ
			Microsoft::WRL::ComPtr<ID3D12Resource> indices; //< Input Geometryの共有COM所有権
			Microsoft::WRL::ComPtr<ID3D12Resource> result; //< AS専用状態の永続Buffer
			Microsoft::WRL::ComPtr<ID3D12Resource> scratch; //< Build完了まで保持するUAV
			std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometries; //< Submesh単位triangle入力
			bool built = false; //< Direct Queueへ記録済み。破棄記録時は無効化する
		};
		struct Frame {
			Microsoft::WRL::ComPtr<ID3D12Resource> result; //< このslotのTLAS
			Microsoft::WRL::ComPtr<ID3D12Resource> scratch; //< Build/Updateの大きい方を確保
			Microsoft::WRL::ComPtr<ID3D12Resource> instances; //< CPU転送先Upload Buffer
			std::vector<D3D12_RAYTRACING_INSTANCE_DESC> previous; //< GPU完了済みslotの比較snapshot
			std::vector<std::shared_ptr<Blas>> references; //< TLASが読むBLASの共有所有権
			std::vector<std::shared_ptr<Blas>> pendingBuilds; //< 記録失敗時にbuiltを戻す対象
			std::vector<D3D12_RAYTRACING_INSTANCE_DESC> current; //< capacity再利用する一時入力
			bool built = false; //< 前回のTLAS有効状態
		};
		/** \brief immutable Mesh世代からAS入力を検証・生成する。まだBuild命令は発行しない */
		[[nodiscard]] Result<std::shared_ptr<Blas>> GetOrCreateBlas(const MeshResource& mesh);
		Microsoft::WRL::ComPtr<ID3D12Device5> device_; //< Backend内だけで保持するDXR Device
		std::unordered_map<uint64_t, std::shared_ptr<Blas>> cache_; //< Geometry世代->BLAS
		std::vector<Frame> frames_; //< GraphicsSystemのFrame slotと一対一
		ShadowStatistics statistics_; //< 診断用累積Build/Update count
	};
}
