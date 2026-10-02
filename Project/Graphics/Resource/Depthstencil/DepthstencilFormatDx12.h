#pragma once

// directx
#include <dxgiformat.h>

// engine
#include "DepthstencilTypes.h"

namespace NexusEngine {
	/** \brief Backend非依存のDepth FormatをDX12のResource/PSO formatへ変換する */
	[[nodiscard]] constexpr DXGI_FORMAT ToNativeDepthStencilFormat(
		const DepthStencilFormat format) noexcept {
		return format == DepthStencilFormat::D32Float
			? DXGI_FORMAT_D32_FLOAT
			: DXGI_FORMAT_UNKNOWN;
	}
} // namespace NexusEngine
