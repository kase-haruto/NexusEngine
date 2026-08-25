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
	class Shader;
	class TextureResource;
	class VertexBuffer;
	struct SamplerDesc;
	struct TextureDesc;

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

		/** \brief ShaderとVertex Layoutから描画Pipelineを初期化する */
		[[nodiscard]] Result<void> CreateGraphicsPipeline(
			GraphicsPipeline& pipeline,
			Shader vertexShader,
			Shader pixelShader,
			const std::vector<VertexAttribute>& vertexLayout) const;

		/** \brief CPU byte列から第一段階のVertex Bufferを初期化する */
		[[nodiscard]] Result<void> CreateVertexBuffer(
			VertexBuffer& vertexBuffer,
			std::span<const uint8_t> data,
			uint32_t stride) const;

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
