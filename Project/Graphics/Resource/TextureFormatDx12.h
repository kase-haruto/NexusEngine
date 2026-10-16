#pragma once

// directx
#include <dxgiformat.h>

// engine
#include "TextureResource.h"

namespace NexusEngine {
	/** \brief Backend非依存TextureFormatをDX12 Resource/SRV用formatへ一箇所で変換する */
	[[nodiscard]] constexpr DXGI_FORMAT ToNativeTextureFormat(const TextureFormat format) noexcept {
		switch(format) {
		case TextureFormat::Rgba8Unorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
		case TextureFormat::Rgba8UnormSrgb: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		case TextureFormat::Rgba32Float: return DXGI_FORMAT_R32G32B32A32_FLOAT;
		default: return DXGI_FORMAT_UNKNOWN;
		}
	}
} // namespace NexusEngine
