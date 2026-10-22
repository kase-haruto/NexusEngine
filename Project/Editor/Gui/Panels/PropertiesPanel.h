#pragma once
#include "IEditorPanel.h"
#include "Editor/Gui/Properties/PropertyCategoryRegistry.h"

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * PropertiesPanel
 * - カテゴリ切り替えによる専門プロパティ編集UIを管理する
 * - 選択対象と登録済みページに基づいて表示内容を決定する
 * - RuntimeデータやGPUリソースの所有権は持たない
 *---------------------------------------------------------------------------------------*/
class PropertiesPanel final : public IEditorPanel {
public:
    explicit PropertiesPanel(const PropertyCategoryRegistry& registry) : registry_(registry) {}
    std::string_view Id() const noexcept override { return "Properties"; }
    std::string_view TitleKey() const noexcept override { return "panel.properties"; }
    /** \brief 現在の共通選択に対応するカテゴリとページを描画する */
    void Draw(const EditorContext& context) override;
    std::string_view ActiveCategory() const { return activeCategoryId_; }
private:
    const PropertyCategoryRegistry& registry_; //< Workspace所有。Panelより長寿命
    std::string activeCategoryId_; //< 言語変更に影響されない表示状態
};
}
