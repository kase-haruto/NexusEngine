#pragma once
#include "Foundation/Error/Result.h"
#include "Graphics/Model/ModelAssetData.h"
namespace NexusEngine {
	/** \brief 三角形を独立chartへ展開する。材質UVを保持し、専用UVだけを生成する。 */
	[[nodiscard]] Result<void> GenerateLightmapGeometry(ModelAssetData&);
	/** Prepare one mesh at a time to bound upload scratch memory. */
	[[nodiscard]] Result<void> GenerateLightmapGeometry(MeshAssetData&);
	/** Build upload geometry directly from source bytes without cloning the source mesh. */
	[[nodiscard]] Result<MeshAssetData> BuildLightmapGeometry(const MeshAssetData&);
	/** \brief chart配置の列数と行数を返す。頂点生成とベイクで同じ規則を使う。 */
	[[nodiscard]] std::array<uint32_t, 2> LightmapChartGrid(uint32_t triangleCount);
} // namespace NexusEngine
