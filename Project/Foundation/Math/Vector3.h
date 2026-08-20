#pragma once

// c++
#include <cmath>

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * Vector3
	 * - RuntimeとRenderer境界で共有できるDirectX非依存の3次元float値
	 * - 座標系や単位は利用するデータ型が定義する
	 *---------------------------------------------------------------------------------------*/
	struct Vector3 {
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;

		[[nodiscard]] static constexpr Vector3 Zero() noexcept { return {}; }
		[[nodiscard]] static constexpr Vector3 One() noexcept { return { 1.0f, 1.0f, 1.0f }; }
		[[nodiscard]] static constexpr Vector3 Up() noexcept { return { 0.0f, 1.0f, 0.0f }; }
		[[nodiscard]] static constexpr Vector3 Right() noexcept { return { 1.0f, 0.0f, 0.0f }; }
		[[nodiscard]] static constexpr Vector3 Forward() noexcept { return { 0.0f, 0.0f, 1.0f }; }

		[[nodiscard]] constexpr float LengthSquared() const noexcept { return x * x + y * y + z * z; }
		[[nodiscard]] float Length() const noexcept { return std::sqrt(LengthSquared()); }
		[[nodiscard]] Vector3 Normalized() const noexcept {
			const float length = Length();
			return length > 0.0f ? *this / length : Zero();
		}

		[[nodiscard]] static constexpr float Dot(const Vector3& left, const Vector3& right) noexcept {
			return left.x * right.x + left.y * right.y + left.z * right.z;
		}

		[[nodiscard]] static constexpr Vector3 Cross(const Vector3& left, const Vector3& right) noexcept {
			return {
				left.y * right.z - left.z * right.y,
				left.z * right.x - left.x * right.z,
				left.x * right.y - left.y * right.x
			};
		}

		[[nodiscard]] constexpr Vector3 operator+(const Vector3& other) const noexcept {
			return { x + other.x, y + other.y, z + other.z };
		}
		[[nodiscard]] constexpr Vector3 operator-(const Vector3& other) const noexcept {
			return { x - other.x, y - other.y, z - other.z };
		}
		[[nodiscard]] constexpr Vector3 operator*(const float scalar) const noexcept {
			return { x * scalar, y * scalar, z * scalar };
		}
		[[nodiscard]] constexpr Vector3 operator/(const float scalar) const noexcept {
			return { x / scalar, y / scalar, z / scalar };
		}
		[[nodiscard]] friend constexpr bool operator==(Vector3, Vector3) noexcept = default;
	};

} // namespace NexusEngine
