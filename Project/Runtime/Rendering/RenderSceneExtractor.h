#pragma once

// engine
#include "Foundation/Error/Result.h"
#include "Graphics/Renderer/RenderScene.h"

namespace NexusEngine {
	class Scene;

	/*-----------------------------------------------------------------------------------------
	 * RenderSceneExtractor
	 * - Runtime ECSからRenderer向けの値スナップショットを構築する
	 * - Transform更新、Scene所有、GPU Resource生成、描画実行は担当しない
	 *---------------------------------------------------------------------------------------*/
	class RenderSceneExtractor final {
	public:
		/**
		 * \brief 更新済みSceneから1 frame分の描画入力を抽出する
		 * \param scene 抽出元Scene。Componentの値は変更しない
		 * \param aspectRatio 描画Viewportの幅÷高さ
		 * \return 描画スナップショット。不正なCamera設定の場合はエラー
		 * \note 呼び出し前にTransformSystem::Updateを完了しておく必要がある
		 */
		[[nodiscard]] Result<RenderScene> Extract(Scene& scene, float aspectRatio) const;
		/** \brief 呼出側のsnapshot容量を再利用して抽出する。失敗時は結果を描画しないこと */
		[[nodiscard]] Result<void> ExtractInto(Scene& scene, float aspectRatio, RenderScene& output) const;
	};

} // namespace NexusEngine
