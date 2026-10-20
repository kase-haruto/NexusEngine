#pragma once
#include <string_view>

namespace NexusEngine {
class EditorSession;
class SelectionService;
class PropertyEditService;
class TypeRegistry;
class PropertyDrawerRegistry;
class LocalizationService;
class EditorCommandRegistry;
struct EditorContext {
    EditorSession& session; //< 既存編集documentの非所有参照
    SelectionService& selection; //< 全Panel共通の選択
    PropertyEditService& properties; //< Undo付き値編集
    const TypeRegistry& types; //< Runtimeの型記述
    const PropertyDrawerRegistry& drawers; //< Editorの描画拡張
    const LocalizationService& text; //< UTF-8翻訳
    const EditorCommandRegistry& commands; //< 操作要求の送信先
};
/*-----------------------------------------------------------------------------------------
 * IEditorPanel
 * - 登録可能なPanelの最小契約
 * - Begin/Endと表示状態・DockingはPanelRegistryが管理する
 *---------------------------------------------------------------------------------------*/
class IEditorPanel {
public:
    virtual ~IEditorPanel() = default;
    virtual std::string_view Id() const noexcept = 0;
    virtual std::string_view TitleKey() const noexcept = 0;
    /** \brief Panelの内容を描画する \param context 非所有Editorサービス */
    virtual void Draw(const EditorContext& context) = 0;
};
}
