#pragma once

// c++
#include <cstddef>
#include <limits>
#include <optional>

namespace NexusEngine {
	/**
	 * \brief 値を指定境界以上の最小倍数へ切り上げる
	 * \param value 整列するbyte数またはoffset
	 * \param alignment 0以外の整列境界。2の累乗である必要はない
	 * \return 整列済み値。加算がsize_tを超える場合またはalignmentが0の場合はnullopt
	 *
	 * moduloを使うため、GPU API以外の任意alignmentにも利用できる。
	 * `(value + alignment - 1)`形式を直接使わず、加算前にoverflowを検出する。
	 */
	[[nodiscard]] constexpr std::optional<size_t> TryAlignUp(
		const size_t value,
		const size_t alignment) noexcept {
		if(alignment == 0) {
			return std::nullopt;
		}

		const size_t remainder = value % alignment;
		if(remainder == 0) {
			return value;
		}

		const size_t padding = alignment - remainder;
		if(value > (std::numeric_limits<size_t>::max)() - padding) {
			return std::nullopt;
		}
		return value + padding;
	}
} // namespace NexusEngine
