#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <filesystem>
#include <memory>
#include <vector>

#include "Graphics/Shadow/ShadowTypes.h"
#include "Graphics/Device/GraphicsCapabilities.h"
#include "Graphics/Descriptor/DescriptorAllocator.h"
#include "Graphics/Resource/ConstantBuffer.h"
#include "Graphics/Backend/DirectX12/RayTracing/RayTracingScene.h"

namespace NexusEngine {
	class BindlessDescriptorTable;
	class DescriptorManager;
	class CommandQueue;
	class GraphicsContext;
	class GraphicsResourceFactory;

	/** HLSL ShadowConstantsと同じ16byte packing。DXR/Map/Surfaceはこの値契約を共有する。 */
	struct ShadowGpuConstants {
		Matrix4x4 worldViewProjection;
		Matrix4x4 inverseViewProjection;
		Matrix4x4 lightViewProjection;
		std::array<float, 4> lightDirectionAngularRadius {};
		std::array<float, 4> biases {}; //< origin/TMin/TMax/Map bias
		std::array<uint32_t, 4> indices {}; //< Surface SRV/Map SRV/Mask UAV/sample count
		std::array<uint32_t, 4> dimensions {}; //< width/height/skinning/padding
		std::array<Matrix4x4, 64> jointPalette {};
	};
	static_assert(sizeof(ShadowGpuConstants) == 4352);

	/*-----------------------------------------------------------------------------------------
	 * ShadowTexture
	 * - Backend内部でDepth/Mask Resourceと共有Allocator由来のViewを所有する
	 * - GPU同期はShadowBackendがResize/Shutdown前に保証する。Heap本体は生成しない
	 *---------------------------------------------------------------------------------------*/
	struct ShadowTexture {
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		DescriptorAllocation dsv;
		ShaderResourceRef srv;
		ShaderResourceRef uav;
		D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
		uint32_t width = 0, height = 0;
	};

	class ShadowBackend;
	/*-----------------------------------------------------------------------------------------
	 * IShadowTechnique
	 * - Surface入力から共通Visibility Maskを出力するBackend内部契約
	 * - 方式選択、Scene抽出、Lighting合成は担当しない
	 *---------------------------------------------------------------------------------------*/
	class IShadowTechnique {
	public:
		virtual ~IShadowTechnique() = default;
		/**
		 * \brief 方式固有の遮蔽をbackend.maskの全pixelへ書き込む
		 * \param backend 共有Surface/Mask/Root/Descriptorの所有者
		 * \param context Fence待機済みFrame indexと意味ベースDraw命令
		 * \param commands Backend内部に限定したDirect Command List
		 * \param draws Lightingと一致する非所有Geometry入力
		 * \param constants Frame別CBへ転送する評価パラメータ
		 * \param settings 方式に依存しない品質設定
		 * \return 記録結果。失敗時のResourceもGPU完了前には解放しない
		 */
		[[nodiscard]] virtual Result<void> Render(ShadowBackend& backend, GraphicsContext& context,
			ID3D12GraphicsCommandList* commands, std::span<const ShadowDrawItem> draws,
			ShadowGpuConstants constants, const ShadowSettings& settings) = 0;
		[[nodiscard]] virtual ShadowTechnique GetTechnique() const noexcept = 0;
		/** \brief GPU idle済みの終了処理。Partial initializationにも対応する */
		virtual void Shutdown(ShadowBackend& backend) noexcept = 0;
	};

	/*-----------------------------------------------------------------------------------------
	 * ShadowMapTechnique
	 * - Directional Light Depth Shadow MapとMask解決Compute Pipelineを所有する
	 * - RT capabilityや方式選択、Lightingは担当しない
	 *---------------------------------------------------------------------------------------*/
	class ShadowMapTechnique final : public IShadowTechnique {
	public:
		[[nodiscard]] Result<void> Initialize(ShadowBackend& backend);
		[[nodiscard]] Result<void> Render(ShadowBackend&, GraphicsContext&, ID3D12GraphicsCommandList*,
			std::span<const ShadowDrawItem>, ShadowGpuConstants, const ShadowSettings&) override;
		[[nodiscard]] ShadowTechnique GetTechnique() const noexcept override { return ShadowTechnique::ShadowMap; }
		void Shutdown(ShadowBackend&) noexcept override;
		void CompleteRecording(bool submitted) noexcept;
	private:
		ShadowTexture depth_; //< Light空間Depth、DSV/SRVの所有者
		D3D12_RESOURCE_STATES initialState_ = D3D12_RESOURCE_STATE_COMMON; //< 記録破棄時の復帰先
		bool recording_ = false; //< 今回Map命令を記録したか
		Microsoft::WRL::ComPtr<ID3D12PipelineState> resolve_; //< Map->共通Mask compute
	};

	/*-----------------------------------------------------------------------------------------
	 * RayTracedShadowTechnique
	 * - DispatchRays StateObject、Shader Table、Scene ASを所有する
	 * - Scene/Lighting/Editorや方式選択は担当しない
	 *---------------------------------------------------------------------------------------*/
	class RayTracedShadowTechnique final : public IShadowTechnique {
	public:
		void ResetGeometry() noexcept { scene_.ResetGeometry(); }
		void RetainGeometry(std::span<const uint64_t> generations) { scene_.RetainGeometry(generations); }
		[[nodiscard]] Result<void> Initialize(ShadowBackend& backend);
		[[nodiscard]] Result<void> Render(ShadowBackend&, GraphicsContext&, ID3D12GraphicsCommandList*,
			std::span<const ShadowDrawItem>, ShadowGpuConstants, const ShadowSettings&) override;
		[[nodiscard]] ShadowTechnique GetTechnique() const noexcept override { return ShadowTechnique::RayTracing; }
		void Shutdown(ShadowBackend&) noexcept override;
		void CompleteRecording(bool submitted, uint32_t frame) noexcept;
		[[nodiscard]] ShadowStatistics GetStatistics() const noexcept { return scene_.GetStatistics(); }
	private:
		RayTracingScene scene_; //< BLAS/TLAS lifetime担当
		Microsoft::WRL::ComPtr<ID3D12StateObject> pipeline_; //< DXR StateObject所有者
		Microsoft::WRL::ComPtr<ID3D12Resource> shaderTable_; //< 全Frameで不変のShader identifier
		bool recording_ = false; //< 未投入AS Buildを有効と誤認しないための記録状態
	};

	/*-----------------------------------------------------------------------------------------
	 * ShadowBackend
	 * - 共通Surface Depth、Shadow Mask、Raster Prepass、Frame定数領域を所有する
	 * - 方式実行はIShadowTechniqueへ委譲。Native操作を上位Rendererへ公開しない
	 * - Scene管理、GPU Frame Fence待機、Lighting合成は担当しない
	 *---------------------------------------------------------------------------------------*/
	class ShadowBackend final {
	public:
		/** GPUの完了後、RTを使わない次のメッシュでも旧BLASを保持しないよう解放する。 */
		void ResetGeometry() noexcept;
		void RetainGeometry(std::span<const uint64_t> generations);
		[[nodiscard]] Result<void> Initialize(void* nativeDevice, CommandQueue* nativeQueue,
			DescriptorManager* descriptorManager, BindlessDescriptorTable* bindlessTable,
			GraphicsResourceFactory* factory, uint32_t frameCount, GraphicsCapabilities caps,
			const std::filesystem::path& shaders);
		[[nodiscard]] Result<ShaderResourceRef> Render(GraphicsContext&, void* nativeCommands,
			const Matrix4x4&, const DirectionalLightData&, std::span<const ShadowDrawItem>, const ShadowSettings&);
		void Shutdown() noexcept;
		/** \brief GraphicsContextの終了通知。未投入ListはAS/Texture状態をrollbackする */
		static void CompleteRecording(void* owner, bool submitted, uint32_t frame) noexcept;
		[[nodiscard]] ShadowStatistics GetStatistics() const noexcept;
		/** \brief 共有Descriptor管理でDepthまたはMaskを作成する。旧資源破棄はIdle後のみ */
		[[nodiscard]] Result<void> CreateTexture(ShadowTexture& texture, uint32_t width, uint32_t height, bool depth);
		void ReleaseTexture(ShadowTexture& texture) noexcept;
		void Transition(ID3D12GraphicsCommandList* commands, ShadowTexture& texture, D3D12_RESOURCE_STATES state);
		/** \brief 同一Draw snapshotをLight/Camera行列でRasterizeする */
		[[nodiscard]] Result<void> Rasterize(GraphicsContext&, ID3D12GraphicsCommandList*, ShadowTexture&,
			const Matrix4x4&, std::span<const ShadowDrawItem>, ShadowGpuConstants);
		[[nodiscard]] Result<uint64_t> WriteConstants(uint32_t frame, const ShadowGpuConstants& constants);
		/** \brief Pipeline初期化時だけ共通DXC経路でShaderをcompileする */
		[[nodiscard]] Result<std::vector<uint8_t>> Compile(const wchar_t* relativePath, const wchar_t* entry, const wchar_t* profile);

		Microsoft::WRL::ComPtr<ID3D12Device> device; //< DeviceをBackend内で共有所有
		CommandQueue* queue = nullptr; //< GraphicsSystem所有の同期先
		DescriptorManager* descriptors = nullptr; //< 共有Heapの非所有参照
		BindlessDescriptorTable* bindless = nullptr; //< 世代/Fence retireの非所有参照
		GraphicsResourceFactory* resources = nullptr; //< Frame CB生成先、非所有
		GraphicsCapabilities capabilities;
		uint32_t framesInFlight = 0;
		Microsoft::WRL::ComPtr<ID3D12RootSignature> root; //< 共通Shadow定数/AS binding
		ShadowTexture surface; //< Camera Surface Depth
		ShadowTexture mask; //< 共通Visibility Mask
	private:
		Microsoft::WRL::ComPtr<ID3D12PipelineState> depthPipeline_; //< 共通Depth Raster
		std::unique_ptr<ShadowMapTechnique> map_; //< Map資源所有者
		std::unique_ptr<RayTracedShadowTechnique> rayTracing_; //< RT資源所有者
		IShadowTechnique* current_ = nullptr; //< 上記所有者の非所有選択参照
		std::filesystem::path shaderDirectory_;
		std::vector<std::unique_ptr<ConstantBuffer>> constants_; //< Draw数の高水位まで再利用
		size_t constantCursor_ = 0; //< 今回Frameだけの定数割当位置
		bool rtFailed_ = false; //< RT初期化/AS失敗の反復を防ぐ
		bool fallback_ = false; //< 最後の方式選択結果
		ShadowTechnique selected_ = ShadowTechnique::Disabled; //< ログ/Editor用
		ShadowMode previousMode_ = ShadowMode::Auto; //< 明示RT変更時のFallback警告を省かない
		D3D12_RESOURCE_STATES initialSurfaceState_ = D3D12_RESOURCE_STATE_COMMON;
		D3D12_RESOURCE_STATES initialMaskState_ = D3D12_RESOURCE_STATE_COMMON;
		bool recording_ = false; //< Texture状態snapshotが今回記録に対応するか
	};
}

