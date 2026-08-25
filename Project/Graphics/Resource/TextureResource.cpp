#include "TextureResource.h"

// directx
#include <d3d12.h>
#include <wrl/client.h>

namespace NexusEngine {
	class TextureResource::Impl final {
	public:
		Microsoft::WRL::ComPtr<ID3D12Resource> resource; //< Sample元となるDefault Heap Texture
		TextureDesc desc; //< Native Resourceと一致するBackend非依存記述
	};

	TextureResource::TextureResource() noexcept = default;
	TextureResource::~TextureResource() noexcept { Shutdown(); }
	void TextureResource::Commit(void* const nativeResource, const TextureDesc& desc) {
		auto implementation = std::make_unique<Impl>();
		// ComPtr代入で参照を追加し、Uploader側の一時ComPtr解放後もTextureがResourceを所有する。
		implementation->resource = static_cast<ID3D12Resource*>(nativeResource);
		implementation->desc = desc;
		impl_ = std::move(implementation);
	}

	void TextureResource::Shutdown() noexcept { impl_.reset(); }
	void* TextureResource::GetNativeResource() const noexcept {
		return impl_ ? impl_->resource.Get() : nullptr;
	}
	bool TextureResource::IsInitialized() const noexcept { return impl_ != nullptr; }
	uint32_t TextureResource::GetWidth() const noexcept { return impl_ ? impl_->desc.width : 0; }
	uint32_t TextureResource::GetHeight() const noexcept { return impl_ ? impl_->desc.height : 0; }
	TextureFormat TextureResource::GetFormat() const noexcept {
		return impl_ ? impl_->desc.format : TextureFormat::Rgba8Unorm;
	}
} // namespace NexusEngine
