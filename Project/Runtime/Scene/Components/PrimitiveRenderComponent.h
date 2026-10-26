#pragma once
#include "Foundation/Reflection/ReflectionMarkers.h"
NEXUS_REFLECT_COMPONENT(PrimitiveRenderComponent, "PrimitiveRender", "component.primitive", tint, enabled, nodeIndex, staticLighting, meshAsset, baseColorTexture, material, shape)

// c++
#include <array>
#include <cstdint>
#include <string>
#include "Graphics/Material/MaterialOverride.h"
#include "Graphics/Model/PrimitiveShape.h"

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * PrimitiveRenderComponent
	 * - Entityを組み込みPrimitiveの描画対象にするRuntime設定を保持する
	 * - Mesh、Pipeline、GPU Resourceの所有はRenderer側が担当する
	 *---------------------------------------------------------------------------------------*/
	struct PrimitiveRenderComponent {
		static constexpr uint32_t kAllNodes = UINT32_MAX;
		std::array<float, 4> tint { 1.0f, 1.0f, 1.0f, 1.0f }; //< RGBA色倍率
		bool enabled = true;                                   //< 描画抽出の対象か
		uint32_t nodeIndex = kAllNodes; //< 接続されたModelの描画対象Node。既定ではModel全体
		bool staticLighting = true; //< 静的背景としてベイクに含める。動く物体はfalse
        std::string meshAsset; //< Assetルートからの相対パス。空ならメッシュ未設定で描画しない
        std::string baseColorTexture; //< 空ならモデル付属の材質を使用
        MaterialOverride material; //< Entity固有の材質設定。共有Assetは変更しない
        PrimitiveShape shape = PrimitiveShape::Asset; //< 組み込み形状はmeshAssetなしで生成。大きさはTransformで指定
	};

} // namespace NexusEngine
