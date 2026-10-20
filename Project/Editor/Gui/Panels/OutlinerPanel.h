#pragma once

#include "IEditorPanel.h"
#include <string>
#include <vector>
#include "Runtime/Scene/EntityId.h"
#include "Runtime/Scene/Components/PersistentIdComponent.h"

namespace NexusEngine {
	class Level;
	class Scene;

	/*-----------------------------------------------------------------------------------------
	 * OutlinerPanel
	 * - Level内のEntity階層を表示するEditor Panel
	 * - Entityの所有やRuntimeの更新処理は担当しない
	 *---------------------------------------------------------------------------------------*/
	class OutlinerPanel final : public IEditorPanel {
	public:
		//===================================================================*/
		// 				public methods
		//===================================================================*/
		
		/**
		* \brief Guiの表示
		*/
		std::string_view Id() const noexcept override { return "Outliner"; }
		std::string_view TitleKey() const noexcept override { return "panel.outliner"; }
		void Draw(const EditorContext& context) override;
		/** \brief 計測用の階層snapshot再構築回数 */
		size_t HierarchyBuilds() const noexcept { return builds_; }

	private:
		//===================================================================*/
		// 				private methods
		//==================================================================*/
		std::string searchText_; //< 共通SearchBarのUTF-8検索入力
		struct Row {
			PersistentId id; //< 選択・編集用ID
			EntityId local, parent; //< 現在World内の階層
			std::string name, key; //< 表示名と安定ImGui ID
			std::vector<size_t> children; //< snapshot内の子行index
			bool open = false; //< 展開状態
		};
		struct Visible { size_t row, depth; };
		void Rebuild(Level& level);
		void RebuildVisible();
		std::vector<Row> rows_; //< 変更時だけ再構築する階層snapshot
		std::vector<size_t> roots_; //< root行
		std::vector<Visible> visible_; //< 展開・検索後のclip対象
		const Scene* scene_ = nullptr; //< cache識別用。dereferenceしない
		size_t revision_ = 0, generation_ = 0, count_ = 0; //< 無効化条件
		bool visibleDirty_ = true; //< 展開・検索の変更
		PersistentId anchor_; //< Shift範囲選択の起点
		size_t builds_ = 0; //< 計測・診断用
	};

} // namespace NexusEngine
