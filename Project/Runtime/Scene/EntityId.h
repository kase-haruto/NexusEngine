#pragma once

// c++
#include <cstdint>
#include <limits>

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * EntityId
	 * - Scene内のEntity slotと世代番号を組み合わせた識別子
	 * - EntityやComponentの所有権は持たない
	 *---------------------------------------------------------------------------------------*/
	struct EntityId {
		static constexpr uint32_t kInvalidIndex = (std::numeric_limits<uint32_t>::max)();

		uint32_t index = kInvalidIndex; //< Scene内のslot index
		uint32_t generation = 0;        //< slot再利用時に更新する世代番号

		[[nodiscard]] constexpr bool IsValid() const noexcept {
			return index != kInvalidIndex && generation != 0;
		}

		[[nodiscard]] friend constexpr bool operator==(EntityId, EntityId) noexcept = default;
	};

} // namespace NexusEngine
