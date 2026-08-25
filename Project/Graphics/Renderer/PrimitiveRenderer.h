#pragma once

// c++
#include <filesystem>

// engine
#include "Foundation/Error/Result.h"
#include "Graphics/Pipeline/GraphicsPipeline.h"
#include "Graphics/Resource/VertexBuffer.h"
#include "Graphics/Resource/ConstantBuffer.h"
#include "Graphics/Resource/TextureResource.h"
#include "Graphics/Descriptor/ShaderResourceRef.h"
#include "Graphics/Material/MaterialDrawData.h"
#include "GraphicsRenderer.h"

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
		 * \param device GPU Resource生成に使用する非所有Device
		 * \param shaderDirectory Shaderルートディレクトリ
		 * \return Shader CompileからVertexBuffer生成までの初期化結果
		 */
		[[nodiscard]] Result<void> Initialize(const GraphicsRendererInitializationContext& context) override;

		/** \brief Device破棄前にPrimitive用GPU Resourceを解放する */
		void Shutdown() noexcept override;

		/**
		 * \brief PipelineとVertexBufferをBindしてPrimitiveのDraw命令を記録する
		 * \param commandList 描画命令を記録する非所有CommandList
		 */
		void Render(GraphicsContext& context) override;

	private:
		GraphicsResourceFactory* resources_ = nullptr; //< GraphicsSystem所有Factoryへの非所有参照
		GraphicsPipeline pipeline_; //< Primitive ShaderとPSOの所有者
		VertexBuffer vertexBuffer_; //< Primitive形状と頂点色を保持するGPU Buffer
		TextureResource texture_; //< Material検証用の1x1 white Texture
		ConstantBuffer materialBuffer_; //< FrameごとのMaterialDrawData転送先
		ShaderResourceRef textureRef_; //< MaterialDrawDataへindexを渡すTexture SRV
		ShaderResourceRef samplerRef_; //< MaterialDrawDataへindexを渡すSampler
		MaterialDrawData materialDrawData_; //< CPU側Material parameter
	};

} // namespace NexusEngine
