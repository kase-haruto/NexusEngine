#pragma once

// directx
#include <dxgiformat.h>

// engine
#include "TextureResource.h"

namespace NexusEngine {
	/** \brief Backend非依存TextureFormatをDX12 Resource/SRV用formatへ一箇所で変換する */
	[[nodiscard]] constexpr DXGI_FORMAT ToNativeTextureFormat(const TextureFormat format) noexcept {
		return format == TextureFormat::Rgba8UnormSrgb
			? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
			: DXGI_FORMAT_R8G8B8A8_UNORM;
	}
} // namespace NexusEngine
