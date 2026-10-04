#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <memory>

#include "Foundation/Error/Result.h"
#include "Graphics/Resource/Depthstencil/DepthstencilTypes.h"
#include "Graphics/Shader/Shader.h"
#include "PipelineLayout.h"

namespace NexusEngine {
	class RootSignature;
	class GraphicsContext;
	class GraphicsResourceFactory;

	enum class VertexFormat : uint8_t { Float2, Float3, Float4 };
	struct VertexAttribute {
		std::string semantic;                         //< HLSL input semantic名
		uint32_t semanticIndex = 0;                  //< Semantic index
		VertexFormat format = VertexFormat::Float3;  //< CPU Vertex属性形式
		uint32_t offset = 0;                         //< Vertex先頭からのbyte offset
	};

	/*-----------------------------------------------------------------------------------------
	 * GraphicsPipelineDesc
	 * - Graphics API型を公開せず、PSO生成に必要な可変設定を保持する
	 *---------------------------------------------------------------------------------------*/
	struct GraphicsPipelineDesc {
		std::vector<VertexAttribute> vertexLayout; //< Vertex Shader inputと一致するCPU Vertex layout
		DepthStencilFormat depthStencilFormat = DepthStencilFormat::None; //< PSOが接続するDepth Target format
		DepthStencilStateDesc depthStencil; //< Depth TestとDepth Writeの動作
	};

	/*-----------------------------------------------------------------------------------------
	 * GraphicsPipeline
	 * - Shader、PipelineLayout、RootSignature、PSOを一つの描画Pipelineとして所有する
	 *---------------------------------------------------------------------------------------*/
	class GraphicsPipeline final {
	public:
		GraphicsPipeline() noexcept;
		~GraphicsPipeline() noexcept;
		GraphicsPipeline(const GraphicsPipeline&) = delete;
		GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;
		/** \brief PSOとRootSignatureをDevice破棄前に解放する。呼出元がGPU完了を保証する */
		void Shutdown() noexcept;
		/** \brief Native型を含まない反射Binding情報を取得する */
		[[nodiscard]] const PipelineLayout& GetLayout() const noexcept;

	private:
		friend class GraphicsContext;
		friend class GraphicsResourceFactory;
		/**
		 * \brief Shader、明示Vertex Layout、ReflectionからGraphics PSOを生成する
		 * \param nativeDevice Backendが所有する非所有Device。Rendererからは呼べない
		 * \param vertexShader 所有権を移動するVertex Shader
		 * \param pixelShader 所有権を移動するPixel Shader
		 * \param pipelineDesc Vertex LayoutとDepth Test設定を含むBackend非依存のPipeline記述
		 * \return Binding統合、RootSignature、PSO生成結果
		 */
		[[nodiscard]] Result<void> Initialize(
			void* nativeDevice,
			Shader vertexShader,
			Shader pixelShader,
			const GraphicsPipelineDesc& pipelineDesc);
		/** \brief RootSignatureとPSOをCommandListへ設定する */
		void Bind(void* nativeCommandList) const noexcept;
		[[nodiscard]] const RootSignature& GetRootSignature() const noexcept;
		class Impl;
		std::unique_ptr<Impl> impl_; //< Native PSO/RootSignatureとShaderの唯一の所有者
	};

} // namespace NexusEngine
