#include "PrimitiveRenderer.h"

// c++
#include <array>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

// engine
#include "GraphicsContext.h"
#include "GraphicsResourceFactory.h"
#include "Graphics/Resource/SamplerDesc.h"
#include "Foundation/Logging/Logger.h"

namespace NexusEngine {
	namespace {

		/*-----------------------------------------------------------------------------------------
		 * PrimitiveVertex
		 * - Primitive Shaderへ入力する位置と頂点色のCPU Layout
		 *---------------------------------------------------------------------------------------*/
		struct PrimitiveVertex {
			float position[3]; //< Clip空間上のXYZ座標
			float color[4];    //< RGBA頂点色
		};

		// 基盤検証用三角形。形状データをGraphicsSystemへ持たせず、描画責務と同じ場所へ閉じ込める。
		constexpr std::array<PrimitiveVertex, 3> kTriangleVertices = {
			PrimitiveVertex { { 0.0f, 0.5f, 0.0f }, { 1.0f, 0.15f, 0.1f, 1.0f } },
			PrimitiveVertex { { 0.5f, -0.5f, 0.0f }, { 0.1f, 1.0f, 0.2f, 1.0f } },
			PrimitiveVertex { { -0.5f, -0.5f, 0.0f }, { 0.1f, 0.3f, 1.0f, 1.0f } }
		};

	} // namespace

	/////////////////////////////////////////////////////////////////////////////////////////
	// Primitive描画に必要なShader、Pipeline、VertexBufferを初期化する
	/////////////////////////////////////////////////////////////////////////////////////////
Result<void> PrimitiveRenderer::Initialize(
		const GraphicsRendererInitializationContext& context) {
		resources_ = &context.resources;
		// Shaderはこの初期化時にCompileとReflectionを一度だけ行い、Draw中には解析しない。
		Shader vertexShader;
		auto result = vertexShader.Initialize(
			{ context.shaderDirectory / L"Primitive/Primitive.VS.hlsl", L"main", L"vs_6_6" },
			ShaderStage::Vertex);
		if(!result) {
			return std::unexpected(std::move(result.error()));
		}

		Shader pixelShader;
		result = pixelShader.Initialize(
			{ context.shaderDirectory / L"Primitive/Primitive.PS.hlsl", L"main", L"ps_6_6" },
			ShaderStage::Pixel);
		if(!result) {
			return std::unexpected(std::move(result.error()));
		}

		// Reflectionでsemanticを検証しつつ、CPU構造からしか安全に決められないoffsetを明示する。
		const std::vector<VertexAttribute> vertexLayout = {
			{ "POSITION", 0, VertexFormat::Float3, offsetof(PrimitiveVertex, position) },
			{ "COLOR", 0, VertexFormat::Float4, offsetof(PrimitiveVertex, color) }
		};
		result = context.resources.CreateGraphicsPipeline(
			pipeline_, std::move(vertexShader), std::move(pixelShader), vertexLayout);
		if(!result) {
			return std::unexpected(std::move(result.error()));
		}

		// 型付き配列をbyte viewへ変換し、VertexBufferへ形状データのGPU所有を移す。
		const auto vertexBytes = std::as_bytes(std::span(kTriangleVertices));
		result = context.resources.CreateVertexBuffer(
			vertexBuffer_,
			{ reinterpret_cast<const uint8_t*>(vertexBytes.data()), vertexBytes.size() },
			sizeof(PrimitiveVertex));
		if(!result) {
			pipeline_.Shutdown();
			return std::unexpected(std::move(result.error()));
		}

		// 1x1 white Textureでも実際のDefault Heap upload、SRV、Sampler、Material index転送経路を通す。
		// 見た目は従来の頂点色を維持しつつ、将来Texture Assetへ差し替える境界を検証できる。
		constexpr std::array<uint8_t, 4> kWhitePixel = { 255, 255, 255, 255 };
		result = resources_->CreateTexture2D(
			texture_, TextureDesc { 1, 1, TextureFormat::Rgba8Unorm }, kWhitePixel);
		if(!result) {
			Shutdown();
			return std::unexpected(std::move(result.error()));
		}
		auto textureReference = resources_->CreatePersistentTextureShaderResource(texture_);
		if(!textureReference) {
			Shutdown();
			return std::unexpected(std::move(textureReference.error()));
		}
		textureRef_ = *textureReference;

		auto samplerReference = resources_->CreatePersistentSampler(SamplerDesc {});
		if(!samplerReference) {
			Shutdown();
			return std::unexpected(std::move(samplerReference.error()));
		}
		samplerRef_ = *samplerReference;

		result = resources_->CreateConstantBuffer(materialBuffer_, sizeof(MaterialDrawData));
		if(!result) {
			Shutdown();
			return std::unexpected(std::move(result.error()));
		}
		materialDrawData_.textureIndex = textureRef_.index;
		materialDrawData_.samplerIndex = samplerRef_.index;
		return {};
	}

	void PrimitiveRenderer::Shutdown() noexcept {
		// GraphicsSystemは通常Shutdown前にGPU idleを保証する。参照はそれでも共通retire経路へ渡し、
		// 実行中のRenderer差し替えへ拡張した場合にも即時slot再利用を起こさない。
		if(resources_ != nullptr) {
			try {
				auto retire = [&](ShaderResourceRef& reference) {
					if(!reference.IsValid()) return;
					auto result = resources_->RetirePersistentShaderResource(reference);
					if(!result) NEXUS_LOG_ERROR("Graphics", result.error().GetMessageText());
					reference = {};
				};
				retire(textureRef_);
				retire(samplerRef_);
			} catch(...) {
				NEXUS_LOG_ERROR("Graphics", "Unexpected exception while retiring PrimitiveRenderer descriptors.");
			}
		}
		materialBuffer_.Shutdown();
		texture_.Shutdown();
		// PipelineとBufferはいずれもDevice依存なので、GraphicsSystemがDeviceより先に呼び出す。
		vertexBuffer_.Shutdown();
		pipeline_.Shutdown();
		resources_ = nullptr;
	}

	void PrimitiveRenderer::Render(GraphicsContext& context) {
		const uint32_t frameIndex = context.GetFrameIndex();
		if(frameIndex >= materialBuffer_.GetFrameCount()) {
			return;
		}
		const auto materialBytes = std::as_bytes(std::span(&materialDrawData_, 1));
		auto writeResult = materialBuffer_.Write(
			frameIndex,
			{ reinterpret_cast<const uint8_t*>(materialBytes.data()), materialBytes.size() });
		if(!writeResult) {
			NEXUS_LOG_ERROR("Graphics", writeResult.error().GetMessageText());
			return;
		}
		// Rendererは描画意図だけを記述し、Native Command List操作はGraphicsContextへ委譲する。
		context.SetGraphicsPipeline(pipeline_);
		auto descriptorResult = context.SetGraphicsConstantBufferTable(pipeline_, materialBuffer_);
		if(!descriptorResult) {
			NEXUS_LOG_ERROR("Graphics", descriptorResult.error().GetMessageText());
			return;
		}
		context.SetVertexBuffer(vertexBuffer_);
		context.SetPrimitiveTopology(PrimitiveTopology::TriangleList);
		context.Draw(vertexBuffer_.GetVertexCount());
	}

} // namespace NexusEngine
