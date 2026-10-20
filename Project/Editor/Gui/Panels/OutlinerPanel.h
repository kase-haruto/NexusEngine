#pragma once

#include "IEditorPanel.h"
#include <string>

namespace NexusEngine {

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

	private:
		//===================================================================*/
		// 				private methods
		//==================================================================*/
		std::string searchText_; //< 共通SearchBarのUTF-8検索入力
	};

} // namespace NexusEngine
