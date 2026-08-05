#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "Foundation/Error/Result.h"
#include "Graphics/Shader/Shader.h"
#include "PipelineLayout.h"
#include "RootSignature.h"

namespace NexusEngine {

	enum class VertexFormat : uint8_t { Float2, Float3, Float4 };
	struct VertexAttribute {
		std::string semantic;                         //< HLSL input semantic名
		uint32_t semanticIndex = 0;                  //< Semantic index
		VertexFormat format = VertexFormat::Float3;  //< CPU Vertex属性形式
		uint32_t offset = 0;                         //< Vertex先頭からのbyte offset
	};

	/*-----------------------------------------------------------------------------------------
	 * GraphicsPipeline
	 * - Shader、PipelineLayout、RootSignature、PSOを一つの描画Pipelineとして所有する
	 *---------------------------------------------------------------------------------------*/
	class GraphicsPipeline final {
	public:
		/**
		 * \brief Shader、明示Vertex Layout、ReflectionからGraphics PSOを生成する
		 * \param device RootSignatureとPSO生成に使う非所有Device
		 * \param vertexShader 所有権を移動するVertex Shader
		 * \param pixelShader 所有権を移動するPixel Shader
		 * \param vertexLayout CPU Vertex構造を表す明示Layout
		 * \return Binding統合、RootSignature、PSO生成結果
		 */
		[[nodiscard]] Result<void> Initialize(ID3D12Device* device, Shader vertexShader, Shader pixelShader, const std::vector<VertexAttribute>& vertexLayout);
		/** \brief PSOとRootSignatureをDevice破棄前に解放する */
		void Shutdown() noexcept;
		/** \brief RootSignatureとPSOをCommandListへ設定する */
		void Bind(ID3D12GraphicsCommandList* commandList) const noexcept;
		/** \brief Material等が名前をHandleへ解決するためのLayoutを取得する */
		[[nodiscard]] const PipelineLayout& GetLayout() const noexcept;
		[[nodiscard]] const RootSignature& GetRootSignature() const noexcept;

	private:
		Shader vertexShader_;
		Shader pixelShader_;
		PipelineLayout layout_;
		RootSignature rootSignature_;
		Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
	};

} // namespace NexusEngine
