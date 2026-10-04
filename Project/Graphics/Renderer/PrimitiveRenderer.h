#pragma once

// c++
#include <filesystem>
#include <memory>
#include <vector>

// engine
#include "Foundation/Error/Result.h"
#include "Graphics/Pipeline/GraphicsPipeline.h"
#include "Graphics/Resource/MeshResource.h"
#include "Graphics/Model/ModelResource.h"
#include "Graphics/Model/ModelInstance.h"
#include "Graphics/Resource/ConstantBuffer.h"
#include "Graphics/Resource/TextureResource.h"
#include "Graphics/Descriptor/ShaderResourceRef.h"
#include "Graphics/Material/PrimitiveDrawData.h"
#include "GraphicsRenderer.h"
#include "RenderScene.h"
#include "Graphics/Shadow/ShadowSystem.h"
#include "Graphics/RayTracing/OpticalSystem.h"

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * PrimitiveRenderer
	 * - 基盤検証用PrimitiveのGeometry、Shader Pipeline、Draw処理を所有する
	 * - Frame同期、RenderTarget、Present、Scene管理は担当しない
	 *---------------------------------------------------------------------------------------*/
	class PrimitiveRenderer final : public IGraphicsRenderer {
	public:
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
		/** \brief Materialごとの光学設定を公開する。GPU参照はRendererが所有する */
		[[nodiscard]] std::span<const OpticalSurfaceMaterial> GetOpticalMaterials() const noexcept { return opticalMaterials_; }
		/** \brief 次FrameのRT評価へ光学係数を反映する。Geometry/BLAS再生成は不要 */
		[[nodiscard]] Result<void> SetOpticalMaterial(uint32_t slot, const OpticalMaterial& optics);


		/** \brief Scene所有のCPUモデルと更新済み姿勢を接続する。初期化前に設定し、Shutdownまで保持すること */
		void SetModelInput(const ModelAssetData* asset, const ModelInstance* pose) noexcept {
			modelAsset_ = asset;
			modelPose_ = pose;
		}

	private:
		struct MaterialGpuResource {
			std::unique_ptr<TextureResource> texture;
			ShaderResourceRef textureRef;
			ShaderResourceRef samplerRef;
		};

		GraphicsResourceFactory* resources_ = nullptr; //< GraphicsSystem所有Factoryへの非所有参照
		GraphicsPipeline pipeline_; //< Primitive ShaderとPSOの所有者
		ShadowSystem shadows_; //< Shadow方式とGPU影Resourceの独立所有者
		OpticalSystem optics_; //< Reflection/Transmission Radianceの独立所有者
		std::vector<OpticalDrawItem> opticalDraws_;
		std::vector<OpticalSurfaceMaterial> opticalMaterials_;
		std::vector<ShadowDrawItem> shadowDraws_; //< capacityを維持する描画Geometry snapshot
		ModelResource model_; //< Scene指定モデルのGPU Resource
		const ModelAssetData* modelAsset_ = nullptr; //< Sceneが選択・ロードしたCPU Asset（非所有）
		const ModelInstance* modelPose_ = nullptr; //< Sceneが更新するCPU姿勢（非所有）
		std::vector<MaterialGpuResource> materialResources_; //< Material slot別Texture/Sampler所有
		std::vector<std::vector<std::unique_ptr<ConstantBuffer>>> meshDrawBuffers_; //< SubmeshごとのFrame安全なDraw Data
		PrimitiveDrawData drawData_; //< 抽出済み行列とMaterial parameterのCPU側転送値
		const RenderScene* renderScene_ = nullptr; //< Component参照を持たないApplication所有snapshot
		std::vector<std::vector<std::vector<std::unique_ptr<ConstantBuffer>>>> extraDrawBuffers_; //< 2件目以降のframe安全なobject/mesh/submesh転送領域
	};

} // namespace NexusEngine
