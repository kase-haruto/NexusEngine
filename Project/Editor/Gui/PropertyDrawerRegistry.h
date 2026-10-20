#pragma once
#include "Runtime/Reflection/TypeRegistry.h"
#include "Editor/Services/LocalizationService.h"
#include "Editor/Gui/Core/GuiItem.h"

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * PropertyDrawerRegistry
 * - PropertyKindと描画関数の対応を管理する
 * - Component型やWorldへの書込み権限を持たない
 *---------------------------------------------------------------------------------------*/
class PropertyDrawerRegistry final {
public:
    using Drawer = std::function<UI::ItemResult(const UI::ItemOptions&, PropertyValue&, const PropertyDescriptor&, const LocalizationService&)>;
    /** \brief 型に対応するUIを登録する。追加型はこのAPIで拡張する */
    void Register(PropertyKind kind, Drawer drawer) { drawers_[kind] = std::move(drawer); }
    /** \brief 値コピーだけを描画・編集する */
    UI::ItemResult Draw(const UI::ItemOptions& options, PropertyValue& value, const PropertyDescriptor& property, const LocalizationService& text) const;
    /** \brief 標準のスカラー・vector・enum Drawerを登録する */
    void RegisterDefaults();
private:
    std::map<PropertyKind, Drawer> drawers_; //< UI側の型別拡張ポイント
};
}
