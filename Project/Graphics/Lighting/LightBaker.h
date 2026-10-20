#pragma once
#include "BakedLightingData.h"
namespace NexusEngine {
	struct ModelAssetData;
	struct RenderScene;
	class ModelInstance;
	/*-----------------------------------------------------------------------------------------
	 * LightBaker
	 * - 静的形状のBVHで間接光を事前計算し、ライトマップとプローブを生成する
	 * - ゲーム中の描画、GPU、Descriptor、ファイル保存は担当しない
	 *---------------------------------------------------------------------------------------*/
	class LightBaker final {
	public:
		/** \brief 現在の静的SceneをCPUでベイクする。処理は明示的な編集操作時だけ実行する。 */
		[[nodiscard]] Result<BakedLightingAsset> Bake(const ModelAssetData&, const ModelInstance&, const RenderScene&, const BakedLightingSettings&) const;
		/** \brief geometry・材質・静的配置・照明からキャッシュの照合値を求める。 */
		[[nodiscard]] static uint64_t Signature(const ModelAssetData&, const ModelInstance&, const RenderScene&);
	};
} // namespace NexusEngine
