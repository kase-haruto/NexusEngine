#pragma once

// c++
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

// engine
#include "Foundation/Math/Matrix4x4.h"

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * RenderObjectId
	 * - Renderer入力とPicking結果を対応付ける不透明な識別値
	 * - Entity indexやgenerationなど上位層の識別方式をGraphicsへ公開しない
	 *---------------------------------------------------------------------------------------*/
	struct RenderObjectId {
		uint64_t value = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
		[[nodiscard]] friend constexpr bool operator==(RenderObjectId, RenderObjectId) noexcept = default;
	};

	/*-----------------------------------------------------------------------------------------
	 * RenderCamera
	 * - 1 frameの描画に必要なCamera Matrixを保持するRenderer入力値
	 * - Camera選択やMatrix計算は担当しない
	 *---------------------------------------------------------------------------------------*/
	struct RenderCamera {
		RenderObjectId sourceObject;       //< Pickingや診断に使う生成元Object
		Matrix4x4 viewMatrix;              //< World空間からView空間への変換
		Matrix4x4 projectionMatrix;        //< View空間からClip空間への変換
		Matrix4x4 viewProjectionMatrix;    //< view * projection
	};

	/*-----------------------------------------------------------------------------------------
	 * PrimitiveRenderItem
	 * - 組み込みPrimitive 1個分のRenderer入力を保持する
	 * - Scene ComponentやGPU Resourceへの参照は保持しない
	 *---------------------------------------------------------------------------------------*/
	struct PrimitiveRenderItem {
		RenderObjectId sourceObject;                            //< Pickingや診断に使う生成元Object
		Matrix4x4 worldMatrix;                                  //< 抽出時点のWorld変換
		std::array<float, 4> tint { 1.0f, 1.0f, 1.0f, 1.0f }; //< RGBA色倍率
	};

	/*-----------------------------------------------------------------------------------------
	 * RenderScene
	 * - 上位Runtimeから受け取る1 frame分のRenderer入力を所有する
	 * - Scene、Entity、Component Storageの型とLifetimeには依存しない
	 *---------------------------------------------------------------------------------------*/
	struct RenderScene {
		std::optional<RenderCamera> camera;          //< Camera未配置のSceneでは空
		std::vector<PrimitiveRenderItem> primitives; //< 組み込みPrimitive描画項目
	};

} // namespace NexusEngine
