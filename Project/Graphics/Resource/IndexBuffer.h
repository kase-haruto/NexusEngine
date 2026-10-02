#pragma once

// c++
#include <cstdint>
#include <memory>

namespace NexusEngine {
	class GraphicsContext;
	class IndexBufferUploader;

	/** Index Bufferが保持する1要素の形式。 */
	enum class IndexFormat : uint8_t {
		UInt16,
		UInt32
	};

	/*-----------------------------------------------------------------------------------------
	 * IndexBuffer
	 * - Upload済みDefault Heap Index ResourceとViewを所有する
	 * - CPU data、Upload staging、Draw命令は担当しない
	 *---------------------------------------------------------------------------------------*/
	class IndexBuffer final {
	public:
		IndexBuffer() noexcept;
		~IndexBuffer() noexcept;
		IndexBuffer(const IndexBuffer&) = delete;
		IndexBuffer& operator=(const IndexBuffer&) = delete;
		IndexBuffer(IndexBuffer&&) = delete;
		IndexBuffer& operator=(IndexBuffer&&) = delete;

		/** \brief GPU Index ResourceとViewを解放する */
		void Shutdown() noexcept;
		[[nodiscard]] bool IsInitialized() const noexcept;
		[[nodiscard]] uint32_t GetIndexCount() const noexcept;
		[[nodiscard]] IndexFormat GetFormat() const noexcept;

	private:
		friend class GraphicsContext;
		friend class IndexBufferUploader;
		/** \brief Upload完了済みNative Resourceの所有権とView情報をcommitする */
		void Commit(void* nativeResource, uint32_t sizeInBytes, IndexFormat format, uint32_t indexCount);
		/** \brief 現在のCommand ListのInput AssemblerへIndex Viewを設定する */
		void Bind(void* nativeCommandList) const noexcept;

		class Impl;
		std::unique_ptr<Impl> impl_;
	};
} // namespace NexusEngine
