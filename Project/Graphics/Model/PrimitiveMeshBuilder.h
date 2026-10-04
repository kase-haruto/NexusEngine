#pragma once
#include "ModelAssetData.h"
#include "Foundation/Error/Result.h"
namespace NexusEngine {
	/*-----------------------------------------------------------------------------------------
	 * PrimitiveMeshBuilder
	 * - ModelVertex形式のCPU Sphere/Box Geometryを生成する
	 * - GPU upload、Material所有、Scene配置は担当しない
	 *---------------------------------------------------------------------------------------*/
	class PrimitiveMeshBuilder final {
	public:
		/** \brief 外向き法線のUV Sphere。radius>0、rings>=3、sectors>=3を検証する */
		[[nodiscard]] static Result<MeshAssetData> Sphere(float radius, uint32_t rings, uint32_t sectors, uint32_t material);
		/** \brief 原点中心の単位Box（各軸-0.5〜0.5）、面ごとの法線とUV */
		[[nodiscard]] static MeshAssetData Box(uint32_t material);
	};
}
