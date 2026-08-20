#pragma once

#include <cstdint>
#include <string_view>

#include "GuiTypes.h"

namespace NexusEngine::UI {
	enum class ItemStatus : uint8_t {
		None = 0,
		Hovered = 1 << 0,
		Active = 1 << 1,
		Focused = 1 << 2,
		Edited = 1 << 3,
		Activated = 1 << 4,
		Deactivated = 1 << 5
	};

	[[nodiscard]] constexpr ItemStatus operator|(const ItemStatus left, const ItemStatus right) noexcept {
		return static_cast<ItemStatus>(static_cast<uint8_t>(left) | static_cast<uint8_t>(right));
	}

	[[nodiscard]] constexpr bool HasStatus(const ItemStatus value, const ItemStatus status) noexcept {
		return (static_cast<uint8_t>(value) & static_cast<uint8_t>(status)) != 0;
	}

	struct ItemOptions {
		ItemId id;
		std::string_view label;
		std::string_view tooltip;
		Vector2 size;
		bool enabled = true;
		bool visible = true;
	};

	/* boolだけでは失われるHoverや編集区間を、Undo/Redo等へ伝える共通結果。 */
	struct ItemResult {
		ItemStatus status = ItemStatus::None;

		[[nodiscard]] constexpr bool Changed() const noexcept {
			return HasStatus(status, ItemStatus::Edited);
		}
		[[nodiscard]] constexpr bool EditFinished() const noexcept {
			return Changed() && HasStatus(status, ItemStatus::Deactivated);
		}
	};
} // namespace NexusEngine::UI
