#pragma once

// c++
#include <array>
#include <cstdint>

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
	};

} // namespace NexusEngine
