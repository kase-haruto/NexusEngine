#pragma once

// c++
#include <cstdint>
#include <cstddef>
#include <span>
#include <vector>

// engine
#include "Foundation/Error/Result.h"
#include "Graphics/Descriptor/ShaderResourceRef.h"
#include "Graphics/Pipeline/GraphicsPipeline.h"

namespace NexusEngine {
	class BindlessDescriptorTable;
	class CommandQueue;
	class ConstantBuffer;
	class IndexBuffer;
	class MeshResource;
	class ModelResource;
	class Shader;
	class TextureResource;
	class VertexBuffer;
	struct SamplerDesc;
	struct TextureDesc;
	enum class IndexFormat : uint8_t;
	struct SubmeshRange;
	struct ModelAssetData;

	/*-----------------------------------------------------------------------------------------
	 * GraphicsResourceFactory
	 * - Renderer用GPU Resource生成をBackend Deviceへ転送する公開境界
	 * - RendererへNative Deviceの型・所有権・寿命管理を公開しない
	 * - 生成したResource本体の所有権は呼び出し側Rendererが保持する
	 *---------------------------------------------------------------------------------------*/
	class GraphicsResourceFactory final {
	public:
		GraphicsResourceFactory() noexcept = default;
		GraphicsResourceFactory(const GraphicsResourceFactory&) = delete;
		GraphicsResourceFactory& operator=(const GraphicsResourceFactory&) = delete;

		/**
		 * \brief ShaderとBackend非依存のPipeline記述から描画Pipelineを初期化する
		 * \param pipeline 作成したPSOとRoot Signatureの所有先
		 * \param vertexShader 所有権をPipelineへ移すVertex Shader
		 * \param pixelShader 所有権をPipelineへ移すPixel Shader
		 * \param pipelineDesc Vertex LayoutとDepth Stateを含むPipeline記述
		 */
		[[nodiscard]] Result<void> CreateGraphicsPipeline(
			GraphicsPipeline& pipeline,
			Shader vertexShader,
			Shader pixelShader,
			const GraphicsPipelineDesc& pipelineDesc) const;

		/** \brief CPU byte列から第一段階のVertex Bufferを初期化する */
		[[nodiscard]] Result<void> CreateVertexBuffer(
			VertexBuffer& vertexBuffer,
			std::span<const uint8_t> data,
			uint32_t stride) const;

		/**
		 * \brief CPU Index byte列からDefault Heap Index Bufferを生成する
		 * \param indexBuffer upload完了後のResource所有先
		 * \param data UInt16またはUInt32 Indexのbyte列
		 * \param format dataのIndex形式
		 */
		[[nodiscard]] Result<void> CreateIndexBuffer(
			IndexBuffer& indexBuffer,
			std::span<const uint8_t> data,
			IndexFormat format) const;

		/**
		 * \brief Vertex/Index dataとSubmesh範囲からGPU Mesh Resourceを作成する
		 * \note 途中失敗時は生成済みBufferをrollbackし、meshを未初期化に保つ
		 */
		[[nodiscard]] Result<void> CreateMeshResource(
			MeshResource& mesh,
			std::span<const uint8_t> vertexData,
			uint32_t vertexStride,
			std::span<const uint8_t> indexData,
			IndexFormat indexFormat,
			const std::vector<SubmeshRange>& submeshes) const;

		/**
		 * \brief CPU Model Assetから複数GPU MeshとNode hierarchyを所有するResourceを生成する
		 * \note 1 Meshでも失敗した場合はModel全体をrollbackする
		 */
		[[nodiscard]] Result<void> CreateModelResource(
			ModelResource& model,
			const ModelAssetData& assetData) const;

		/**
		 * \brief 全FrameContextに独立sliceを持つConstant Bufferを初期化する
		 * \param constantBuffer 生成Resourceの所有先
		 * \param dataSize 各Frameで書き込む論理byte数
		 */
		[[nodiscard]] Result<void> CreateConstantBuffer(
			ConstantBuffer& constantBuffer,
			size_t dataSize) const;
		/** \brief Constant Bufferの各Frame sliceへ対応する永続CBV参照を生成する */
		[[nodiscard]] Result<std::vector<ShaderResourceRef>> CreatePersistentConstantBufferShaderResources(
			const ConstantBuffer& constantBuffer) const;

		/**
		 * \brief RGBA pixel列をDefault Heap 2D Textureへ同期uploadする
		 * \note File decodeは上位Asset Loaderの責務とし、検証済みpixel列だけを受け取る
		 */
		[[nodiscard]] Result<void> CreateTexture2D(
			TextureResource& texture,
			const TextureDesc& desc,
			std::span<const uint8_t> pixels) const;
		/** \brief PNG/JPEG等のencoded画像をdecodeし、sRGB Textureとしてuploadする */
		[[nodiscard]] Result<void> CreateTexture2DFromEncodedData(
			TextureResource& texture,
			std::span<const uint8_t> encodedData) const;

		/** \brief 完成済みTextureから永続Bindless SRV参照を生成する */
		[[nodiscard]] Result<ShaderResourceRef> CreatePersistentTextureShaderResource(
			const TextureResource& texture) const;
		/** \brief Backend非依存設定から永続Bindless Sampler参照を生成する */
		[[nodiscard]] Result<ShaderResourceRef> CreatePersistentSampler(const SamplerDesc& desc) const;
		/**
		 * \brief Resource/Sampler参照をQueue末尾のFence完了後に再利用可能にする
		 * \note 呼び出し後、渡したShaderResourceRefをMaterial等から直ちに除去すること
		 */
		[[nodiscard]] Result<void> RetirePersistentShaderResource(ShaderResourceRef reference) const;
		/** \brief generationを含めて現在のBindless参照が利用可能か検証する */
		[[nodiscard]] bool IsPersistentShaderResourceValid(ShaderResourceRef reference) const noexcept;

	private:
		friend class GraphicsSystem;
		void ConnectBackend(
			void* nativeDevice,
			CommandQueue* commandQueue,
			BindlessDescriptorTable* bindlessDescriptors,
			uint32_t framesInFlight) noexcept;

		void* nativeDevice_ = nullptr; //< GraphicsSystem所有Deviceへの非所有参照
		CommandQueue* commandQueue_ = nullptr; //< Upload実行とFence同期に使用する非所有Queue
		BindlessDescriptorTable* bindlessDescriptors_ = nullptr; //< 世代とFence retireを管理する非所有Table
		uint32_t framesInFlight_ = 0; //< Constant Buffer slice数のBackend規約
	};
} // namespace NexusEngine
