#pragma once
#include "IEditorPanel.h"
#include "Editor/Gui/Properties/PropertyCategoryView.h"

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * DetailsPanel
	 * - Level上で選択されたエンティティの詳細を編集する
	 * - ReflectionからUIを構築し、Componentの具体型を参照しない
	 * - 共通ヘッダとカテゴリ別の編集ページを一つのWindowにまとめる
	 *---------------------------------------------------------------------------------------*/
	class DetailsPanel final : public IEditorPanel {
	public:
		//===================================================================*/
		//                    public methods
		//===================================================================*/
		explicit DetailsPanel(const PropertyCategoryRegistry& registry) : categories_(registry) {}
		~DetailsPanel()								 = default;
		DetailsPanel(const DetailsPanel&)			 = delete;
		DetailsPanel& operator=(const DetailsPanel&) = delete;
		std::string_view Id() const noexcept override { return "Details"; }
		std::string_view TitleKey() const noexcept override { return "panel.details"; }
		/** \brief 共有Selectionを解決してプロパティを描画する \param context Editorサービス */
		void Draw(const EditorContext& context) override;
		std::string_view ActiveCategory() const { return categories_.ActiveCategory(); }
	private:
		PropertyCategoryView categories_; //< ページ別の表示状態。編集履歴は共通サービスが所有する
	};

} // namespace NexusEngine
