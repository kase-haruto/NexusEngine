#pragma once

#include <cstdint>
#include <string_view>

namespace NexusEngine::UI {
	struct Vector2 {
		float x = 0.0f;
		float y = 0.0f;
	};

	struct Color {
		float red = 1.0f;
		float green = 1.0f;
		float blue = 1.0f;
		float alpha = 1.0f;
	};

	/* Engine側のTextureを識別する軽量Handle。GPU DescriptorやImGui型を保持しない。 */
	struct TextureHandle {
		uint64_t value = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
	};

	/* 表示Labelと分離した安定ID。Label変更や同名Itemによる衝突を避ける。 */
	struct ItemId {
		uint64_t value = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
		[[nodiscard]] static constexpr ItemId FromString(const std::string_view text) noexcept {
			uint64_t hash = 14695981039346656037ull;
			for(const char character : text) {
				hash ^= static_cast<uint8_t>(character);
				hash *= 1099511628211ull;
			}
			return { hash };
		}
	};
} // namespace NexusEngine::UI
