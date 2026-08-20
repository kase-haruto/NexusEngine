#pragma once

// c++
#include <array>

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * PrimitiveRenderComponent
	 * - Entityを組み込みPrimitiveの描画対象にするRuntime設定を保持する
	 * - Mesh、Pipeline、GPU Resourceの所有はRenderer側が担当する
	 *---------------------------------------------------------------------------------------*/
	struct PrimitiveRenderComponent {
		std::array<float, 4> tint { 1.0f, 1.0f, 1.0f, 1.0f }; //< RGBA色倍率
		bool enabled = true;                                   //< 描画抽出の対象か
	};

} // namespace NexusEngine
