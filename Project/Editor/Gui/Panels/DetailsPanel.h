#pragma once
#include "IEditorPanel.h"

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * DetailsPanel
	 * - Level上で選択されたエンティティの詳細を編集する
	 * - ReflectionからUIを構築し、Componentの具体型を参照しない
	 *---------------------------------------------------------------------------------------*/
	class DetailsPanel final : public IEditorPanel {
	public:
		//===================================================================*/
		//                    public methods
		//===================================================================*/
		DetailsPanel()								 = default;
		~DetailsPanel()								 = default;
		DetailsPanel(const DetailsPanel&)			 = delete;
		DetailsPanel& operator=(const DetailsPanel&) = delete;
		std::string_view Id() const noexcept override { return "Details"; }
		std::string_view TitleKey() const noexcept override { return "panel.details"; }
		/** \brief 共有Selectionを解決してプロパティを描画する \param context Editorサービス */
		void Draw(const EditorContext& context) override;


	};

} // namespace NexusEngine
