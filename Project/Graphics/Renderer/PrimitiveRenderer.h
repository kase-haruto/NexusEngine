#pragma once

// c++
#include <filesystem>

// directx
#include <d3d12.h>

// engine
#include "Foundation/Error/Result.h"
#include "Graphics/Pipeline/GraphicsPipeline.h"
#include "Graphics/Resource/VertexBuffer.h"

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * PrimitiveRenderer
	 * - 基盤検証用PrimitiveのGeometry、Shader Pipeline、Draw処理を所有する
	 * - Frame同期、RenderTarget、Present、Scene管理は担当しない
	 *---------------------------------------------------------------------------------------*/
	class PrimitiveRenderer final {
	public:
		/**
		 * \brief Primitive用Shader、Pipeline、Geometry Bufferを初期化する
		 * \param device GPU Resource生成に使用する非所有Device
		 * \param shaderDirectory Shaderルートディレクトリ
		 * \return Shader CompileからVertexBuffer生成までの初期化結果
		 */
		[[nodiscard]] Result<void> Initialize(ID3D12Device* device, const std::filesystem::path& shaderDirectory);

		/** \brief Device破棄前にPrimitive用GPU Resourceを解放する */
		void Shutdown() noexcept;

		/**
		 * \brief PipelineとVertexBufferをBindしてPrimitiveのDraw命令を記録する
		 * \param commandList 描画命令を記録する非所有CommandList
		 */
		void Draw(ID3D12GraphicsCommandList* commandList) const noexcept;

	private:
		GraphicsPipeline pipeline_; //< Primitive ShaderとPSOの所有者
		VertexBuffer vertexBuffer_; //< Primitive形状と頂点色を保持するGPU Buffer
	};

} // namespace NexusEngine
