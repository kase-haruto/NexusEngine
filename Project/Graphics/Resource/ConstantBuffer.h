#pragma once

// c++
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

// engine
#include "Foundation/Error/Result.h"

namespace NexusEngine {
	class GraphicsResourceFactory;

	/*-----------------------------------------------------------------------------------------
	 * ConstantBuffer
	 * - CPUから頻繁に更新する定数データ用Upload Resourceを所有する
	 * - GPU実行中のsliceを上書きしないようFrameごとに独立した領域を持つ
	 * - CBV Descriptor、Material binding、Frame index選択は担当しない
	 *---------------------------------------------------------------------------------------*/
	class ConstantBuffer final {
	public:
		/** DirectX 12 CBVのBufferLocationとSizeInBytesが要求するbyte境界。 */
		static constexpr size_t kDataAlignment = 256;

		ConstantBuffer() noexcept;
		~ConstantBuffer() noexcept;
		ConstantBuffer(const ConstantBuffer&) = delete;
		ConstantBuffer& operator=(const ConstantBuffer&) = delete;
		ConstantBuffer(ConstantBuffer&&) = delete;
		ConstantBuffer& operator=(ConstantBuffer&&) = delete;

		/**
		 * \brief 指定Frame sliceの先頭へ定数データをコピーする
		 * \param frameIndex GraphicsSystemが使用するFrameContext index
		 * \param data 論理data size以下のコピー元byte列
		 * \return index、size、初期化状態を検証した更新結果
		 * \note dataより後ろのpadding領域は初期化時に0で埋められた状態を維持する
		 */
		[[nodiscard]] Result<void> Write(uint32_t frameIndex, std::span<const uint8_t> data);

		/** \brief Device破棄前にmappingを解除してGPU Resourceを解放する */
		void Shutdown() noexcept;

		[[nodiscard]] size_t GetDataSize() const noexcept;
		[[nodiscard]] size_t GetAlignedSliceSize() const noexcept;
		[[nodiscard]] uint32_t GetFrameCount() const noexcept;
		/** \brief 将来CBVを生成するためのFrame slice先頭GPU仮想Addressを取得する */
		[[nodiscard]] uint64_t GetGpuAddress(uint32_t frameIndex) const noexcept;

	private:
		friend class GraphicsResourceFactory;
		[[nodiscard]] Result<void> Initialize(void* nativeDevice, size_t dataSize, uint32_t frameCount);

		class Impl;
		std::unique_ptr<Impl> impl_; //< DirectX 12 Resourceとpersistent mappingを隠す唯一の所有者
	};
} // namespace NexusEngine
