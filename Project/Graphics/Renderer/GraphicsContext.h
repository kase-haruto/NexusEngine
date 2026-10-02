#pragma once

// c++
#include <cstdint>

// engine
#include "Foundation/Error/Result.h"
#include "Graphics/Descriptor/ShaderResourceRef.h"

namespace NexusEngine {
	class GraphicsPipeline;
	class VertexBuffer;
	class ConstantBuffer;
	class BindlessDescriptorTable;
	class TransientDescriptorArena;

	/** 描画Backendに依存しないPrimitive topology。 */
	enum class PrimitiveTopology : uint8_t {
		TriangleList
	};

	/*-----------------------------------------------------------------------------------------
	 * GraphicsContext
	 * - Rendererが現在のFrameへ描画命令を記録するためのBackend非依存境界
	 * - Native Command List、Resource Barrier、Present、Frame同期は公開しない
	 * - GraphicsSystemがFrame記録中だけ生成する非所有の一時View
	 *---------------------------------------------------------------------------------------*/
	class GraphicsContext final {
	public:
		GraphicsContext(const GraphicsContext&) = delete;
		GraphicsContext& operator=(const GraphicsContext&) = delete;

		/** \brief Drawで使用するGraphics Pipelineを設定する */
		void SetGraphicsPipeline(const GraphicsPipeline& pipeline) noexcept;
		/**
		 * \brief Pipelineの従来型Resource/Sampler table先頭へBindless参照を設定する
		 * \return 必要なtable参照が有効で設定できた場合true
		 */
		[[nodiscard]] bool SetGraphicsDescriptorTables(
			const GraphicsPipeline& pipeline,
			ShaderResourceRef resourceTable,
			ShaderResourceRef samplerTable = {}) noexcept;
		/**
		 * \brief 現在FrameのConstant Buffer sliceから一時CBVを生成してResource tableへ設定する
		 * \return Arena容量不足やPipeline layout不一致を含む設定結果
		 * \note 生成Descriptorは現在FrameのFence完了後まで有効
		 */
		[[nodiscard]] Result<void> SetGraphicsConstantBufferTable(
			const GraphicsPipeline& pipeline,
			const ConstantBuffer& constantBuffer);
		/** \brief Input Assemblerのslot 0へVertex Bufferを設定する */
		void SetVertexBuffer(const VertexBuffer& vertexBuffer) noexcept;
		/** \brief 後続DrawのPrimitive topologyを設定する */
		void SetPrimitiveTopology(PrimitiveTopology topology) noexcept;
		/** \brief 非Index描画命令を現在のFrameへ記録する */
		void Draw(uint32_t vertexCount, uint32_t instanceCount = 1,
		          uint32_t firstVertex = 0, uint32_t firstInstance = 0) noexcept;
		/** \brief Frame-local Resource slice選択に使用する現在のFrameContext index */
		[[nodiscard]] uint32_t GetFrameIndex() const noexcept { return frameIndex_; }
		/**
		 * \brief Native backendと直接接続するRender Extension用のCommand List参照
		 * \note 通常のRendererはこの逃げ道を使用せず、GraphicsContextの意味ベースAPIを使用すること
		 */
		[[nodiscard]] void* GetNativeCommandList() const noexcept { return nativeCommandList_; }

	private:
		friend class GraphicsSystem;
		explicit GraphicsContext(
			void* nativeCommandList,
			BindlessDescriptorTable* bindlessDescriptors,
			TransientDescriptorArena* transientDescriptors,
			uint32_t frameIndex) noexcept;

		void* nativeCommandList_ = nullptr; //< GraphicsSystem所有Command ListのFrame中だけ有効な非所有参照
		BindlessDescriptorTable* bindlessDescriptors_ = nullptr; //< Descriptor世代検証とGPU Handle解決先
		TransientDescriptorArena* transientDescriptors_ = nullptr; //< Fence後に再利用する一時Descriptor生成先
		uint32_t frameIndex_ = 0; //< 現在再利用可能になったFrameContext index
	};
} // namespace NexusEngine
