#include "PrimitiveRenderer.h"

// c++
#include <array>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

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
		ID3D12Device* const device,
		const std::filesystem::path& shaderDirectory) {
		// Shaderはこの初期化時にCompileとReflectionを一度だけ行い、Draw中には解析しない。
		Shader vertexShader;
		auto result = vertexShader.Initialize(
			{ shaderDirectory / L"Primitive/Primitive.VS.hlsl", L"main", L"vs_6_6" },
			ShaderStage::Vertex);
		if(!result) {
			return std::unexpected(std::move(result.error()));
		}

		Shader pixelShader;
		result = pixelShader.Initialize(
			{ shaderDirectory / L"Primitive/Primitive.PS.hlsl", L"main", L"ps_6_6" },
			ShaderStage::Pixel);
		if(!result) {
			return std::unexpected(std::move(result.error()));
		}

		// Reflectionでsemanticを検証しつつ、CPU構造からしか安全に決められないoffsetを明示する。
		const std::vector<VertexAttribute> vertexLayout = {
			{ "POSITION", 0, VertexFormat::Float3, offsetof(PrimitiveVertex, position) },
			{ "COLOR", 0, VertexFormat::Float4, offsetof(PrimitiveVertex, color) }
		};
		result = pipeline_.Initialize(device, std::move(vertexShader), std::move(pixelShader), vertexLayout);
		if(!result) {
			return std::unexpected(std::move(result.error()));
		}

		// 型付き配列をbyte viewへ変換し、VertexBufferへ形状データのGPU所有を移す。
		const auto vertexBytes = std::as_bytes(std::span(kTriangleVertices));
		result = vertexBuffer_.Initialize(
			device,
			{ reinterpret_cast<const uint8_t*>(vertexBytes.data()), vertexBytes.size() },
			sizeof(PrimitiveVertex));
		if(!result) {
			pipeline_.Shutdown();
			return std::unexpected(std::move(result.error()));
		}
		return {};
	}

	void PrimitiveRenderer::Shutdown() noexcept {
		// PipelineとBufferはいずれもDevice依存なので、GraphicsSystemがDeviceより先に呼び出す。
		vertexBuffer_.Shutdown();
		pipeline_.Shutdown();
	}

	void PrimitiveRenderer::Draw(ID3D12GraphicsCommandList* const commandList) const noexcept {
		// RootSignature/PSOとGeometryを揃えてから、一つのTriangle Listとして描画する。
		pipeline_.Bind(commandList);
		vertexBuffer_.Bind(commandList);
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		commandList->DrawInstanced(vertexBuffer_.GetVertexCount(), 1, 0, 0);
	}

} // namespace NexusEngine
