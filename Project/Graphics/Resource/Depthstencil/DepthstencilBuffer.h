#pragma once

// c++
#include <cstdint>
#include <memory>

// engine
#include "DepthstencilTypes.h"
#include "Foundation/Error/Result.h"

namespace NexusEngine {
	class DescriptorAllocator;
	class GraphicsSystem;

	/*-----------------------------------------------------------------------------------------
	 * DepthStencilBuffer
	 * - Depth Textureと対応するDSV Descriptorを所有する
	 * - ClearやOutput MergerへのBindはGraphicsSystemが担当する
	 * - Native Graphics API型はImplに隠蔽する
	 *---------------------------------------------------------------------------------------*/
	class DepthStencilBuffer final {
	public:
		DepthStencilBuffer() noexcept;
		~DepthStencilBuffer() noexcept;
		DepthStencilBuffer(const DepthStencilBuffer&) = delete;
		DepthStencilBuffer& operator=(const DepthStencilBuffer&) = delete;

		/** \brief GPU ResourceとDSV Descriptorを解放し、未初期化状態へ戻す */
		void Shutdown() noexcept;
		/** \brief 有効なDepth Resourceを所有しているか返す */
		[[nodiscard]] bool IsInitialized() const noexcept;
		[[nodiscard]] uint32_t GetWidth() const noexcept;
		[[nodiscard]] uint32_t GetHeight() const noexcept;
		[[nodiscard]] DepthStencilFormat GetFormat() const noexcept;

	private:
		friend class GraphicsSystem;
		/**
		 * \brief Depth TextureとDSV Descriptorを初期化する
		 * \param nativeDevice GraphicsSystemが所有するNative Deviceへの非所有参照
		 * \param dsvAllocator DescriptorManagerが所有するDSV allocatorへの非所有参照
		 * \param desc 作成するDepth BufferのサイズとFormat
		 */
		[[nodiscard]] Result<void> Initialize(
			void* nativeDevice,
			DescriptorAllocator* dsvAllocator,
			const DepthStencilBufferDesc& desc);
		/**
		 * \brief DSV slotを維持したままDepth Textureを新しいサイズで再生成する
		 * \return 旧Resourceを破壊せずに再生成した結果
		 */
		[[nodiscard]] Result<void> Resize(void* nativeDevice, uint32_t width, uint32_t height);
		/** \brief GraphicsSystemのOutput Merger操作に限定してNative DSV Handle値を返す */
		[[nodiscard]] uint64_t GetNativeDsvHandle() const noexcept;

		class Impl;
		std::unique_ptr<Impl> impl_;
	};

} // namespace NexusEngine
