#pragma once

// c++
#include <cstdint>
#include <memory>

namespace NexusEngine {
	class GraphicsResourceFactory;
	class TextureUploader;

	/** ShaderからsampleするTextureのpixel format。Graphics API固有formatはBackendで変換する。 */
	enum class TextureFormat : uint8_t {
		Rgba8Unorm,
		Rgba8UnormSrgb
	};

	/*-----------------------------------------------------------------------------------------
	 * TextureDesc
	 * - 2D Textureの論理寸法とpixel formatを保持するBackend非依存記述
	 * - 現段階では1 mip、1 array slice、4byte RGBA pixelを対象とする
	 *---------------------------------------------------------------------------------------*/
	struct TextureDesc {
		uint32_t width = 0;
		uint32_t height = 0;
		TextureFormat format = TextureFormat::Rgba8Unorm;
	};

	/*-----------------------------------------------------------------------------------------
	 * TextureResource
	 * - Upload完了済みDefault Heap 2D Textureを所有する
	 * - File decode、Upload staging、Descriptor、Sampler、Materialは担当しない
	 *---------------------------------------------------------------------------------------*/
	class TextureResource final {
	public:
		TextureResource() noexcept;
		~TextureResource() noexcept;
		TextureResource(const TextureResource&) = delete;
		TextureResource& operator=(const TextureResource&) = delete;
		TextureResource(TextureResource&&) = delete;
		TextureResource& operator=(TextureResource&&) = delete;

		/** \brief GPU完了保証後にTexture Resourceを解放する */
		void Shutdown() noexcept;

		[[nodiscard]] bool IsInitialized() const noexcept;
		[[nodiscard]] uint32_t GetWidth() const noexcept;
		[[nodiscard]] uint32_t GetHeight() const noexcept;
		[[nodiscard]] TextureFormat GetFormat() const noexcept;

	private:
		friend class GraphicsResourceFactory;
		friend class TextureUploader;
		/** \brief Upload成功後のNative Resourceを参照追加して所有状態へcommitする */
		void Commit(void* nativeResource, const TextureDesc& desc);
		/** \brief FactoryがSRVを生成するためのNative Resource非所有参照を取得する */
		[[nodiscard]] void* GetNativeResource() const noexcept;
		class Impl;
		std::unique_ptr<Impl> impl_; //< Native Textureを公開Headerから隠す唯一の所有者
	};
} // namespace NexusEngine
