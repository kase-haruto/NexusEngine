#pragma once

// engine
#include "Foundation/Error/Result.h"
#include "Runtime/Scene/Entity.h"

namespace NexusEngine {
	class Scene;

	/*-----------------------------------------------------------------------------------------
	 * TransformSystem
	 * - Local TransformからLocal/World Matrixを計算し、親子階層を評価する
	 * - Entity、Component、描画Resourceは所有しない
	 *---------------------------------------------------------------------------------------*/
	class TransformSystem final {
	public:
		/**
		 * \brief Entityへ新しい親を設定する
		 * \param scene EntityとHierarchyComponentを所有するScene
		 * \param child 親を変更するEntity
		 * \param parent 新しい親Entity
		 * \return 無効Entity、Transform不足、循環の場合はエラー
		 */
		[[nodiscard]] Result<void> SetParent(Scene& scene, Entity child, Entity parent) const;

		/**
		 * \brief EntityをTransform階層のrootへ戻す
		 * \return 有効なEntityを処理した場合true
		 */
		bool ClearParent(Scene& scene, Entity child) const;

		/**
		 * \brief Scene内の全TransformについてLocal/World Matrixを更新する
		 * \note 直接編集で循環したHierarchyを検出した場合はエラーを返す
		 */
		[[nodiscard]] Result<void> Update(Scene& scene) const;
	};

} // namespace NexusEngine
