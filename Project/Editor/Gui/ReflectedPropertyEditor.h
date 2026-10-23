#pragma once
#include "Editor/Gui/Panels/IEditorPanel.h"
#include <string_view>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * ReflectedPropertyEditor
 * - Detailsと専門ページから共通Drawer・Asset Widget・編集transactionを利用する
 * - プロパティの並びやカテゴリ、Componentの具体型は決定しない
 *---------------------------------------------------------------------------------------*/
/** \brief 指定したReflection値を既存のDrawerと編集サービスで描画する
 *  \param primaryOnly trueなら主選択のみ編集する。MaterialのSource Slot対応に使用する */
void DrawReflectedProperty(const EditorContext& context, std::string_view typeId, std::string_view propertyId,
    bool primaryOnly = false);
}
