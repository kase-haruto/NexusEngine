#pragma once

// c++
#include <cmath>

// engine
#include "Vector3.h"

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * Quaternion
	 * - Runtime Transformの3次元回転を表すDirectX非依存値
	 * - Editor用Euler角やMatrixへの変換結果は保持しない
	 *---------------------------------------------------------------------------------------*/
	struct Quaternion {
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
		float w = 1.0f;

		[[nodiscard]] static constexpr Quaternion Identity() noexcept { return {}; }

		[[nodiscard]] float LengthSquared() const noexcept {
			return x * x + y * y + z * z + w * w;
		}

		[[nodiscard]] Quaternion Normalized() const noexcept {
			const float lengthSquared = LengthSquared();
			if(lengthSquared <= 0.0f) {
				return Identity();
			}
			const float inverseLength = 1.0f / std::sqrt(lengthSquared);
			return { x * inverseLength, y * inverseLength, z * inverseLength, w * inverseLength };
		}

		[[nodiscard]] static constexpr Quaternion Multiply(const Quaternion& left, const Quaternion& right) noexcept {
			return {
				left.w * right.x + left.x * right.w + left.y * right.z - left.z * right.y,
				left.w * right.y - left.x * right.z + left.y * right.w + left.z * right.x,
				left.w * right.z + left.x * right.y - left.y * right.x + left.z * right.w,
				left.w * right.w - left.x * right.x - left.y * right.y - left.z * right.z
			};
		}

		/** \brief XYZ Euler回転（radian）からQuaternionを生成する */
		[[nodiscard]] static Quaternion FromEulerRadians(const Vector3& euler) noexcept {
			const float halfX = euler.x * 0.5f;
			const float halfY = euler.y * 0.5f;
			const float halfZ = euler.z * 0.5f;
			const Quaternion rotationX { std::sin(halfX), 0.0f, 0.0f, std::cos(halfX) };
			const Quaternion rotationY { 0.0f, std::sin(halfY), 0.0f, std::cos(halfY) };
			const Quaternion rotationZ { 0.0f, 0.0f, std::sin(halfZ), std::cos(halfZ) };
			return Multiply(Multiply(rotationZ, rotationY), rotationX).Normalized();
		}

		[[nodiscard]] friend constexpr bool operator==(Quaternion, Quaternion) noexcept = default;
	};

} // namespace NexusEngine
