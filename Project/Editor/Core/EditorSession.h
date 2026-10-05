#pragma once

#include "Runtime/Level/Level.h"
#include "Runtime/Scene/Components/PersistentIdComponent.h"
#include "Runtime/Scene/Components/TransformComponent.h"
#include <cmath>

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * EditorSession
	 * - 編集用Level、永続IDによる選択、編集状態を所有する
	 * - ImGui、GPU、通信には依存しない。同一Process内の編集を担当する
	 *---------------------------------------------------------------------------------------*/
	class EditorSession final {
	public:
		/** \brief 既存選択を解除して編集用Worldを生成する */
		[[nodiscard]] Result<void> Create();
		/** \brief World参照を破棄する前に選択状態を解除する */
		void Close() noexcept;
		/** \brief 現在のWorldで解決できる永続IDだけを選択する */
		void Select(PersistentId id);

		[[nodiscard]] PersistentId GetSelection() const noexcept { return selected_; }
		[[nodiscard]] Entity	   GetSelectedEntity() { return level_.FindEntity(selected_); }
		[[nodiscard]] Level&	   GetLevel() noexcept { return level_; }
		[[nodiscard]] bool		   IsDirty() const noexcept { return dirty_; }

		/** \brief IDとLocal値による編集を適用する。派生MatrixはLevel更新で再計算する */
		[[nodiscard]] bool SetTransform(PersistentId id, Vector3 translation, Quaternion rotation, Vector3 scale);

	private:
		Level		 level_{"EditorLevel"}; //< 編集用World。再生用Worldとは共有しない
		PersistentId selected_;				//< EntityやComponentのアドレスを保持しない
		bool		 dirty_ = false;		//< 編集操作が適用されたか
	};
} // namespace NexusEngine
