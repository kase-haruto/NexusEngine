#pragma once

// engine
#include "Runtime/Level/LevelDocument.h"
#include "Runtime/Scene/Components/PersistentIdComponent.h"

// c++ standard
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
		/** Open an existing level document; preserve the current session on failure. */
		[[nodiscard]] Result<void> Open(const std::filesystem::path& path);
		/** \brief World参照を破棄する前に選択状態を解除する */
		void Close() noexcept;
		/** \brief 現在のWorldで解決できる永続IDだけを選択する */
		void Select(PersistentId id);

		[[nodiscard]] PersistentId	 GetSelection() const noexcept { return selected_; }
		[[nodiscard]] Entity		 GetSelectedEntity();
		[[nodiscard]] LevelDocument* GetLevelDocument() const noexcept { return levelDocument_.get(); }
		[[nodiscard]] Level*		 GetLevel() noexcept { return levelDocument_ ? levelDocument_->GetLevel() : nullptr; }
		[[nodiscard]] bool			 IsDirty() const noexcept { return dirty_; }

		/** \brief IDとLocal値による編集を適用する。派生MatrixはLevel更新で再計算する */
		[[nodiscard]] bool SetTransform(PersistentId id, Vector3 translation, Quaternion rotation, Vector3 scale);
		/** \brief 静的ベイク対象か、動的なプローブ受光対象かを永続ID付きで編集する。 */
		[[nodiscard]] bool SetStaticLighting(PersistentId id, bool enabled);

	private:
		std::unique_ptr<LevelDocument> levelDocument_; //< 編集用World。再生用Worldとは共有しない

		PersistentId selected_;		 //< EntityやComponentのアドレスを保持しない
		bool		 dirty_ = false; //< 編集操作が適用されたか
	};
} // namespace NexusEngine
