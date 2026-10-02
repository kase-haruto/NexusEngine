#include "GraphicsPipeline.h"

#include "Graphics/Resource/Depthstencil/DepthstencilFormatDx12.h"

#include <utility>

namespace NexusEngine {
	namespace {
		constexpr int32_t kPipelineCreationFailed = 1;
		constexpr int32_t kInputMismatch = 2;
		constexpr int32_t kInvalidState = 3;
		constexpr int32_t kInvalidDepthState = 4;

		[[nodiscard]] constexpr D3D12_COMPARISON_FUNC ToNativeCompareOperation(const CompareOperation operation) noexcept {
			switch(operation) {
			case CompareOperation::Never: return D3D12_COMPARISON_FUNC_NEVER;
			case CompareOperation::Equal: return D3D12_COMPARISON_FUNC_EQUAL;
			case CompareOperation::LessEqual: return D3D12_COMPARISON_FUNC_LESS_EQUAL;
			case CompareOperation::Greater: return D3D12_COMPARISON_FUNC_GREATER;
			case CompareOperation::NotEqual: return D3D12_COMPARISON_FUNC_NOT_EQUAL;
			case CompareOperation::GreaterEqual: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
			case CompareOperation::Always: return D3D12_COMPARISON_FUNC_ALWAYS;
			case CompareOperation::Less:
			default: return D3D12_COMPARISON_FUNC_LESS;
			}
		}
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	// Shader Metadataと明示Vertex LayoutからGraphics Pipelineを生成する
	/////////////////////////////////////////////////////////////////////////////////////////
	Result<void> GraphicsPipeline::Initialize(
		ID3D12Device* const device,
		Shader vertexShader,
		Shader pixelShader,
		const GraphicsPipelineDesc& pipelineDesc) {
		if(device == nullptr || pipelineState_) {
			return std::unexpected(Error(ErrorCategory::Graphics, kInvalidState, "Graphics pipeline arguments or state are invalid."));
		}
		// Depth TargetのFormatがないPSOでTest/Writeを有効にすると、Output MergerとPSOの契約が成立しない。
		if((pipelineDesc.depthStencil.depthTestEnabled || pipelineDesc.depthStencil.depthWriteEnabled) &&
		   pipelineDesc.depthStencilFormat == DepthStencilFormat::None) {
			return std::unexpected(Error(ErrorCategory::Graphics, kInvalidDepthState,
				"A depth format is required when depth testing or writing is enabled."));
		}
		const auto& vertexLayout = pipelineDesc.vertexLayout;

		// 全要素の生成成功後だけメンバへコミットし、再試行可能な失敗状態を保つ。
		PipelineLayout layout;
		// Stage Bindingを先に統合し、register衝突をRootSignature生成前に検出する。
		auto layoutResult = layout.Build(vertexShader.GetMetadata(), pixelShader.GetMetadata());
		if(!layoutResult) return layoutResult;
		RootSignature rootSignature;
		auto rootResult = rootSignature.Initialize(device, layout);
		if(!rootResult) return rootResult;

		// Reflectionはsemanticを検証するために使い、CPU構造のstrideやoffsetは明示Layoutを正とする。
		if(vertexLayout.size() != vertexShader.GetMetadata().inputs.size()) {
			return std::unexpected(Error(ErrorCategory::Graphics, kInputMismatch, "Explicit vertex layout does not match reflected input count."));
		}
		std::vector<D3D12_INPUT_ELEMENT_DESC> elements;
		elements.reserve(vertexLayout.size());
		for(size_t index = 0; index < vertexLayout.size(); ++index) {
			const auto& attribute = vertexLayout[index];
			const auto& reflected = vertexShader.GetMetadata().inputs[index];
			if(attribute.semantic != reflected.name || attribute.semanticIndex != reflected.semanticIndex) {
				return std::unexpected(Error(ErrorCategory::Graphics, kInputMismatch, "Explicit vertex layout semantic does not match shader reflection."));
			}
			// Engine独自VertexFormatをPSO生成直前にだけDXGI_FORMATへ変換する。
			DXGI_FORMAT format = DXGI_FORMAT_R32G32B32_FLOAT;
			if(attribute.format == VertexFormat::Float2) format = DXGI_FORMAT_R32G32_FLOAT;
			if(attribute.format == VertexFormat::Float4) format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			elements.push_back({ attribute.semantic.c_str(), attribute.semanticIndex, format, 0, attribute.offset, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
		}

		const auto vs = vertexShader.GetBytecode();
		const auto ps = pixelShader.GetBytecode();
		// 第一段階のPrimitiveに必要な固定機能状態だけを明示し、MaterialやRenderPass設定は先行実装しない。
		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
		desc.pRootSignature = rootSignature.GetNative();
		desc.VS = { vs.data(), vs.size() };
		desc.PS = { ps.data(), ps.size() };
		desc.BlendState.AlphaToCoverageEnable = FALSE;
		desc.BlendState.IndependentBlendEnable = FALSE;
		const D3D12_RENDER_TARGET_BLEND_DESC defaultBlend { FALSE, FALSE, D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD, D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD, D3D12_LOGIC_OP_NOOP, D3D12_COLOR_WRITE_ENABLE_ALL };
		// 未使用slotも完全な既定値で初期化し、Debug Layerで未定義状態と解釈されないようにする。
		for(auto& target : desc.BlendState.RenderTarget) target = defaultBlend;
		desc.SampleMask = UINT_MAX;
		desc.RasterizerState = { D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_BACK, FALSE, D3D12_DEFAULT_DEPTH_BIAS, D3D12_DEFAULT_DEPTH_BIAS_CLAMP, D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS, TRUE, FALSE, FALSE, 0, D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF };
		desc.DepthStencilState.DepthEnable = pipelineDesc.depthStencil.depthTestEnabled;
		desc.DepthStencilState.DepthWriteMask = pipelineDesc.depthStencil.depthWriteEnabled
			? D3D12_DEPTH_WRITE_MASK_ALL
			: D3D12_DEPTH_WRITE_MASK_ZERO;
		desc.DepthStencilState.DepthFunc = ToNativeCompareOperation(pipelineDesc.depthStencil.compareOperation);
		desc.DepthStencilState.StencilEnable = FALSE;
		desc.DSVFormat = ToNativeDepthStencilFormat(pipelineDesc.depthStencilFormat);
		desc.InputLayout = { elements.data(), static_cast<UINT>(elements.size()) };
		desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		desc.NumRenderTargets = 1;
		desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;
		const HRESULT result = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipelineState));
		if(FAILED(result)) return std::unexpected(MakeDirectXError(kPipelineCreationFailed, result, "Failed to create graphics pipeline state."));
		// PSO生成成功後だけ所有権を確定し、部分初期化状態を公開しない。
		vertexShader_ = std::move(vertexShader);
		pixelShader_ = std::move(pixelShader);
		layout_ = std::move(layout);
		rootSignature_ = std::move(rootSignature);
		pipelineState_ = std::move(pipelineState);
		return {};
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	// Draw前にRootSignatureとPSOをCommandListへ設定する
	/////////////////////////////////////////////////////////////////////////////////////////
	void GraphicsPipeline::Bind(ID3D12GraphicsCommandList* const commandList) const noexcept {
		commandList->SetGraphicsRootSignature(rootSignature_.GetNative());
		commandList->SetPipelineState(pipelineState_.Get());
	}
	/////////////////////////////////////////////////////////////////////////////////////////
	// Device依存Pipeline Resourceを破棄する
	/////////////////////////////////////////////////////////////////////////////////////////
	void GraphicsPipeline::Shutdown() noexcept {
		pipelineState_.Reset();
		rootSignature_ = {};
		layout_ = {};
		vertexShader_ = {};
		pixelShader_ = {};
	}
	const PipelineLayout& GraphicsPipeline::GetLayout() const noexcept { return layout_; }
	const RootSignature& GraphicsPipeline::GetRootSignature() const noexcept { return rootSignature_; }

} // namespace NexusEngine
