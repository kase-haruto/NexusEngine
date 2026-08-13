#pragma once

#include <cstdint>
#include <optional>

#include "GuiTypes.h"

namespace NexusEngine::UI {
	/* Backendが画像描画に必要とする不透明な参照。Widget側では解釈しない。 */
	struct ResolvedTexture {
		uintptr_t nativeId = 0;
		uint32_t width = 0;
		uint32_t height = 0;

		[[nodiscard]] bool IsValid() const noexcept { return nativeId != 0; }
	};

	class ITextureProvider {
	public:
		virtual ~ITextureProvider() = default;
		[[nodiscard]] virtual std::optional<ResolvedTexture> Resolve(TextureHandle texture) const noexcept = 0;
	};
} // namespace NexusEngine::UI
