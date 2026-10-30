#pragma once

// c++
#include <filesystem>
#include <memory>
#include <vector>
#include <unordered_map>
#include <stop_token>

// engine
#include "Foundation/Error/Result.h"
#include "Graphics/Pipeline/GraphicsPipeline.h"
#include "Graphics/Resource/MeshResource.h"
#include "Graphics/Model/ModelResource.h"
#include "Graphics/Model/ModelInstance.h"
#include "Graphics/Resource/ConstantBuffer.h"
#include "Graphics/Resource/TextureResource.h"
#include "Graphics/Descriptor/ShaderResourceRef.h"
#include "Graphics/Resource/SamplerDesc.h"
#include "Graphics/Material/PrimitiveDrawData.h"
#include "GraphicsRenderer.h"
#include "RenderScene.h"
#include "Graphics/Environment/EnvironmentLighting.h"
#include "Graphics/Lighting/BakedLighting.h"
#include "Graphics/Shadow/ShadowSystem.h"
#include "Graphics/RayTracing/OpticalSystem.h"
#include "Graphics/Meshlet/MeshShaderPipeline.h"
#include "Graphics/Meshlet/MeshletResource.h"
#include "Graphics/Selection/SelectionOutline.h"

namespace NexusEngine {
	enum class TextureColorSpace : uint8_t;
	struct RendererModelResources;

	/*-----------------------------------------------------------------------------------------
	 * PrimitiveRenderer
	 * - 基盤検証用PrimitiveのGeometry、Shader Pipeline、Draw処理を所有する
	 * - Frame同期、RenderTarget、Present、Scene管理は担当しない
	 *---------------------------------------------------------------------------------------*/
	class PrimitiveRenderer final : public IGraphicsRenderer {
	public:
		/** 描画中の資源を共有所有で固定し、準備スレッドへ渡す。 */
		std::shared_ptr<const RendererModelResources> CaptureModelResources() const;
		/** プレビューの初回生成でも、メインビューのGPUアセットを共有する。 */
		Result<void> InitializeShared(const GraphicsRendererInitializationContext&, std::shared_ptr<const RendererModelResources>);
		/** Pipelineを初期化せず、追加・変更されたモデル資源だけを準備する。 */
		Result<void> PrepareModel(const GraphicsRendererInitializationContext&, std::shared_ptr<const RendererModelResources>, std::stop_token = {});
		/** GPUの完了後にモデルだけを交換する。描画Pipelineと表示設定は維持する。 */
		void CommitPreparedModel(PrimitiveRenderer& prepared);
		/** メッシュ適用の待ち時間を、頂点数に依存する処理と固定の初期化処理に分けて記録する。 */
		struct InitializationProfile {
			double pipelineMs = 0, selectionMs = 0, geometryMs = 0, texturesMs = 0, viewSystemsMs = 0, meshletsMs = 0, totalMs = 0;
			double shadersMs = 0, pipelineCreationMs = 0, meshletPipelineMs = 0;
			size_t meshes = 0, vertices = 0, materials = 0, encodedTextureBytes = 0;
			size_t meshesUploaded = 0, meshesReused = 0, texturesCreated = 0, texturesReused = 0;
		};
		const InitializationProfile& GetInitializationProfile() const noexcept { return initializationProfile_; }
		/** \brief Editor に設定値のみを公開する。Runtime は ImGui に依存しない */
		[[nodiscard]] MeshletDebugSettings& GetMeshletSettings() noexcept { return meshletSettings_; }
		/** \brief GPU対応と初期化失敗の有無を返す。Pipelineは有効化時に生成する */
		[[nodiscard]] bool AreMeshShadersAvailable() const noexcept { return meshShadersSupported_ && !meshShaderFailed_; }
		/**
		 * \brief Primitive用Shader、Pipeline、Geometry Bufferを初期化する
		 * \param context GPU Resource FactoryとShader/Assetルートを持つ初期化Context
		 * \return Shader CompileからVertexBuffer生成までの初期化結果
		 */
		[[nodiscard]] Result<void> Initialize(const GraphicsRendererInitializationContext& context) override;

		/** \brief Device破棄前にPrimitive用GPU Resourceを解放する */
		void Shutdown() noexcept override;

		/**
		 * \brief PipelineとVertexBufferをBindし、Scene指定モデルのDraw命令を記録する
		 * \param context 現在Frameに描画命令を記録するBackend非依存Context
		 */
		void Render(GraphicsContext& context) override;
		/** \brief Application所有の値snapshotを接続する。Rendererより長く生存すること */
		void SetRenderScene(const RenderScene* scene) noexcept { renderScene_ = scene; }
		/** \brief Editorは設定/統計だけを利用する。ImGuiをRuntimeへ持ち込まない */
		[[nodiscard]] ShadowSystem& GetShadows() noexcept { return shadows_; }
		[[nodiscard]] OpticalSystem& GetOptics() noexcept { return optics_; }
		/** \brief 直近の描画でDXRのGI出力が利用可能か返す。GPU参照をEditorへ公開しない。 */
		[[nodiscard]] bool IsGlobalIlluminationActive() const noexcept { return drawData_.diffuseGIIndex != 0; }
		/** \brief 現在の静的Sceneをベイクして指定キャッシュへ保存する。Editorの明示操作専用。 */
		[[nodiscard]] Result<void> BakeLighting(const BakedLightingSettings&);
		/** \brief GPUを公開せず、ベイク済み照明の読み込み状態を取得する。 */
		[[nodiscard]] const std::string& GetBakedLightingStatus() const noexcept { return bakedLighting_.GetStatus(); }
		/** \brief Materialごとの光学設定を公開する。GPU参照はRendererが所有する */
		[[nodiscard]] std::span<const OpticalSurfaceMaterial> GetOpticalMaterials() const noexcept { return opticalMaterials_; }
		/** \brief 次FrameのRT評価へ光学係数を反映する。Geometry/BLAS再生成は不要 */
		[[nodiscard]] Result<void> SetOpticalMaterial(uint32_t slot, const OpticalMaterial& optics);
		/** GPUの完了後に呼び、メッシュ交換による環境ベイク・描画系の再構築を避ける。 */
		void ReuseViewResources(PrimitiveRenderer& previous) noexcept;


		/** \brief Scene所有のCPUモデルと更新済み姿勢を接続する。初期化前に設定し、Shutdownまで保持すること */
		void SetModelInput(const ModelAssetData* asset, const ModelInstance* pose) noexcept {
			modelAsset_ = asset;
			modelPose_ = pose;
		}

	private:
#if defined(NEXUS_GRAPHICS_TESTS)
		friend class GraphicsBackendTestAccess;
#endif
		friend struct RendererModelResources;
		InitializationProfile initializationProfile_;
		std::shared_ptr<const RendererModelResources> initializationReuse_;
		struct SharedTexture {
			GraphicsResourceFactory* resources = nullptr;
			TextureResource texture;
			ShaderResourceRef reference;
			~SharedTexture();
		};
		struct SharedSampler {
			GraphicsResourceFactory* resources = nullptr;
			ShaderResourceRef reference;
			~SharedSampler();
		};
		Result<void> BuildModelResources(const GraphicsRendererInitializationContext&, const RendererModelResources*, std::stop_token);
		Result<void> EnsureMeshlets(const RendererModelResources*, std::stop_token, bool createPipeline);
		Result<ShaderResourceRef> GetMaterialSampler(const SamplerDesc&, const RendererModelResources* = nullptr);
		Result<std::shared_ptr<SharedTexture>> GetMaterialTexture(uint64_t, const std::vector<uint8_t>&, TextureColorSpace, const RendererModelResources*);
		std::vector<std::pair<SamplerDesc, std::shared_ptr<SharedSampler>>> materialSamplers_;
		std::unordered_map<uint64_t, std::shared_ptr<SharedTexture>> modelTextures_; //< 現snapshotが使う画像だけを所有する
		struct MaterialGpuResource {
			std::shared_ptr<SharedTexture> texture;
			ShaderResourceRef textureRef;
			ShaderResourceRef samplerRef;
			std::shared_ptr<SharedTexture> normalTexture, metallicRoughnessTexture;
			std::shared_ptr<SharedTexture> emissiveTexture, occlusionTexture;
			ShaderResourceRef emissiveRef, emissiveSamplerRef, occlusionRef, occlusionSamplerRef;
			ShaderResourceRef normalRef, normalSamplerRef, metallicRoughnessRef, metallicRoughnessSamplerRef;
		};

		GraphicsResourceFactory* resources_ = nullptr; //< GraphicsSystem所有Factoryへの非所有参照
		GraphicsPipeline pipeline_; //< Primitive ShaderとPSOの所有者
        SelectionOutline selectionOutline_; //< 独立Maskから深度なしで外周を合成
        std::vector<SelectionOutlineDraw> selectionDraws_; //< 通常Drawの確定済みCBをFrame内だけ借用する
		MeshShaderPipeline meshPipeline_; //< 独立 AS/MS/PS 経路
		std::vector<std::shared_ptr<MeshletResource>> meshletResources_; //< 元Meshと同じindex。既存形状のMeshletを共有する
		std::vector<bool> meshletAttempted_;
		bool meshShadersSupported_ = false, meshShaderFailed_ = false;
		std::filesystem::path shaderDirectory_;
		MeshletDebugSettings meshletSettings_; //< Editor が変更する表示 / Culling 設定
		ShadowSystem shadows_; //< Shadow方式とGPU影Resourceの独立所有者
		EnvironmentLighting environment_; //< IBL GPU ownership independent of materials and viewport
		BakedLighting bakedLighting_; //< 保存済みGIのGPU所有者
		OpticalSystem optics_; //< Reflection/Transmission Radianceの独立所有者
		std::vector<OpticalDrawItem> opticalDraws_;
		std::vector<OpticalSurfaceMaterial> opticalMaterials_;
		std::vector<ShadowDrawItem> shadowDraws_; //< capacityを維持する描画Geometry snapshot
		ModelResource model_; //< Scene指定モデルのGPU Resource
		const ModelAssetData* modelAsset_ = nullptr; //< Sceneが選択・ロードしたCPU Asset（非所有）
		const ModelInstance* modelPose_ = nullptr; //< Sceneが更新するCPU姿勢（非所有）
		std::vector<MaterialGpuResource> materialResources_; //< Material slot別Texture/Sampler所有
        std::vector<std::unique_ptr<ConstantBuffer>> drawBuffers_; //< Nodeを共有するMeshもDrawごとに独立したFrame安全な転送領域
		PrimitiveDrawData drawData_; //< 抽出済み行列とMaterial parameterのCPU側転送値
		const RenderScene* renderScene_ = nullptr; //< Component参照を持たないApplication所有snapshot
	};

} // namespace NexusEngine
