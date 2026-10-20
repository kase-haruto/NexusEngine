#pragma once

// c++
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

// engine
#include "Foundation/Math/Matrix4x4.h"
#include "Graphics/Shadow/ShadowTypes.h"
#include "Graphics/Fog/FogSettings.h"
#include "Graphics/Lighting/GlobalIlluminationSettings.h"
#include "Graphics/Lighting/BakedLightingData.h"
#include "Graphics/Lighting/DDGISettings.h"
#include "Graphics/Environment/EnvironmentLightingData.h"

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
		static constexpr uint32_t kAllNodes = UINT32_MAX;
		RenderObjectId sourceObject;                            //< Pickingや診断に使う生成元Object
		Matrix4x4 worldMatrix;                                  //< 抽出時点のWorld変換
		std::array<float, 4> tint { 1.0f, 1.0f, 1.0f, 1.0f }; //< RGBA色倍率
		uint32_t nodeIndex = kAllNodes; //< Model全体または指定Nodeのみを描画する
        bool staticLighting = true; //< falseの物体はベイクから除外し、プローブで受光
        uint64_t lightingId = 0; //< 再ロード後も変わらないベイク照合ID。0はsourceObjectを使用
	};

	/*-----------------------------------------------------------------------------------------
	 * RenderScene
	 * - 上位Runtimeから受け取る1 frame分のRenderer入力を所有する
	 * - Scene、Entity、Component Storageの型とLifetimeには依存しない
	 *---------------------------------------------------------------------------------------*/
	struct RenderScene {
		DirectionalLightData directionalLight; //< API非依存Lightingと影の共通入力
		std::array<float, 3> ambientIrradiance {0.03f, 0.03f, 0.03f};
		FogSettings fog;
		BakedLightingSettings bakedLighting; //< 保存済みのライトマップ・プローブ設定
		DDGISettings ddgi; //< 動的な照度フィールド。GPU参照は保持しない
        GlobalIlluminationSettings globalIllumination; //< 拡散RTGIの品質設定
		EnvironmentLightingData environment; //< CPU settings only
		std::optional<RenderCamera> camera;          //< Camera未配置のSceneでは空
		std::vector<PrimitiveRenderItem> primitives; //< 組み込みPrimitive描画項目
	};

} // namespace NexusEngine
