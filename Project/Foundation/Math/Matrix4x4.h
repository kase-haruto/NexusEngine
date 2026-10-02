#pragma once

// c++
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <utility>

// engine
#include "Quaternion.h"
#include "Vector3.h"

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * Matrix4x4
	 * - CalyxEngineと同じrow-vector規則の4x4 Matrix値を保持する
	 * - GPUへの転送形式やDirectX固有Matrix型への変換はRenderer境界が担当する
	 *---------------------------------------------------------------------------------------*/
	struct Matrix4x4 {
		std::array<float, 16> elements = {
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		};

		[[nodiscard]] constexpr float& At(const std::size_t row, const std::size_t column) noexcept {
			return elements[row * 4 + column];
		}

		[[nodiscard]] constexpr float At(const std::size_t row, const std::size_t column) const noexcept {
			return elements[row * 4 + column];
		}

		[[nodiscard]] static constexpr Matrix4x4 Identity() noexcept { return {}; }
	};

	[[nodiscard]] constexpr Matrix4x4 Multiply(const Matrix4x4& left, const Matrix4x4& right) noexcept {
		Matrix4x4 result;
		result.elements.fill(0.0f);
		for(std::size_t row = 0; row < 4; ++row) {
			for(std::size_t column = 0; column < 4; ++column) {
				for(std::size_t element = 0; element < 4; ++element) {
					result.At(row, column) += left.At(row, element) * right.At(element, column);
				}
			}
		}
		return result;
	}

	/** \brief 行列を転置し、法線行列などrow/columnの入れ替えが必要な用途へ使用する */
	[[nodiscard]] constexpr Matrix4x4 Transpose(const Matrix4x4& matrix) noexcept {
		Matrix4x4 result;
		for(std::size_t row = 0; row < 4; ++row) {
			for(std::size_t column = 0; column < 4; ++column) {
				result.At(row, column) = matrix.At(column, row);
			}
		}
		return result;
	}

	[[nodiscard]] constexpr Matrix4x4 MakeScaleMatrix(const Vector3& scale) noexcept {
		Matrix4x4 result;
		result.At(0, 0) = scale.x;
		result.At(1, 1) = scale.y;
		result.At(2, 2) = scale.z;
		return result;
	}

	[[nodiscard]] constexpr Matrix4x4 MakeTranslationMatrix(const Vector3& translation) noexcept {
		Matrix4x4 result;
		result.At(3, 0) = translation.x;
		result.At(3, 1) = translation.y;
		result.At(3, 2) = translation.z;
		return result;
	}

	/**
	 * \brief 前方+Z、深度範囲0～1の透視投影Matrixを生成する
	 * \note fovY、aspect、nearClip、farClipの妥当性は呼び出し側で検証する
	 */
	[[nodiscard]] inline Matrix4x4 MakePerspectiveFovMatrix(
		const float fovY,
		const float aspect,
		const float nearClip,
		const float farClip) noexcept {
		Matrix4x4 result;
		result.elements.fill(0.0f);
		const float yScale = 1.0f / std::tan(fovY * 0.5f);
		const float zRange = farClip - nearClip;
		result.At(0, 0) = yScale / aspect;
		result.At(1, 1) = yScale;
		result.At(2, 2) = farClip / zRange;
		result.At(2, 3) = 1.0f;
		result.At(3, 2) = -nearClip * farClip / zRange;
		return result;
	}

	/**
	 * \brief 一般の4x4 Matrixの逆行列を計算する
	 * \return 逆行列。特異Matrixの場合はstd::nullopt
	 */
	[[nodiscard]] inline std::optional<Matrix4x4> TryInverse(const Matrix4x4& matrix) noexcept {
		std::array<std::array<float, 8>, 4> augmented {};
		for(std::size_t row = 0; row < 4; ++row) {
			for(std::size_t column = 0; column < 4; ++column) {
				augmented[row][column] = matrix.At(row, column);
			}
			augmented[row][row + 4] = 1.0f;
		}

		for(std::size_t pivotColumn = 0; pivotColumn < 4; ++pivotColumn) {
			std::size_t pivotRow = pivotColumn;
			for(std::size_t row = pivotColumn + 1; row < 4; ++row) {
				if(std::abs(augmented[row][pivotColumn]) > std::abs(augmented[pivotRow][pivotColumn])) {
					pivotRow = row;
				}
			}
			if(std::abs(augmented[pivotRow][pivotColumn]) <= 1.0e-6f) {
				return std::nullopt;
			}
			if(pivotRow != pivotColumn) {
				std::swap(augmented[pivotRow], augmented[pivotColumn]);
			}

			const float pivot = augmented[pivotColumn][pivotColumn];
			for(float& value : augmented[pivotColumn]) {
				value /= pivot;
			}
			for(std::size_t row = 0; row < 4; ++row) {
				if(row == pivotColumn) {
					continue;
				}
				const float factor = augmented[row][pivotColumn];
				for(std::size_t column = 0; column < 8; ++column) {
					augmented[row][column] -= factor * augmented[pivotColumn][column];
				}
			}
		}

		Matrix4x4 result;
		for(std::size_t row = 0; row < 4; ++row) {
			for(std::size_t column = 0; column < 4; ++column) {
				result.At(row, column) = augmented[row][column + 4];
			}
		}
		return result;
	}

	[[nodiscard]] inline Matrix4x4 MakeRotationMatrix(const Quaternion& value) noexcept {
		const Quaternion rotation = value.Normalized();
		const float xx = rotation.x * rotation.x;
		const float yy = rotation.y * rotation.y;
		const float zz = rotation.z * rotation.z;
		const float xy = rotation.x * rotation.y;
		const float xz = rotation.x * rotation.z;
		const float yz = rotation.y * rotation.z;
		const float wx = rotation.w * rotation.x;
		const float wy = rotation.w * rotation.y;
		const float wz = rotation.w * rotation.z;

		Matrix4x4 result;
		result.At(0, 0) = 1.0f - 2.0f * (yy + zz);
		result.At(0, 1) = 2.0f * (xy + wz);
		result.At(0, 2) = 2.0f * (xz - wy);
		result.At(1, 0) = 2.0f * (xy - wz);
		result.At(1, 1) = 1.0f - 2.0f * (xx + zz);
		result.At(1, 2) = 2.0f * (yz + wx);
		result.At(2, 0) = 2.0f * (xz + wy);
		result.At(2, 1) = 2.0f * (yz - wx);
		result.At(2, 2) = 1.0f - 2.0f * (xx + yy);
		return result;
	}

	/** \brief row-vector規則でScale、Rotation、Translationの順に適用する */
	[[nodiscard]] inline Matrix4x4 MakeAffineMatrix(
		const Vector3& scale,
		const Quaternion& rotation,
		const Vector3& translation) noexcept {
		return Multiply(
			Multiply(MakeScaleMatrix(scale), MakeRotationMatrix(rotation)),
			MakeTranslationMatrix(translation));
	}

} // namespace NexusEngine
