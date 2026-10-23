#pragma once
#include "Editor/Gui/Core/GuiItem.h"
#include <cstddef>
#include <array>
#include <span>
#include <string>

namespace NexusEngine::UI {
/*-----------------------------------------------------------------------------------------
 * PropertyWidgets
 * - 安定ID、共通Style、編集区間を持つ再利用可能なProperty入力部品
 * - Reflection、Component、Localization、Undo履歴には依存しない
 *---------------------------------------------------------------------------------------*/
struct EnumOption {
    int value = 0; //< 列挙値。表示順と独立して扱う
    std::string_view label; //< 呼出中だけ有効な翻訳済み表示名
};
/** \brief boolを編集する \param options 表示名と独立したID、Tooltip、編集可否 */
ItemResult PropertyBool(const ItemOptions& options, bool& value);
/** \brief 整数を編集し、開始・変更・確定を返す */
ItemResult PropertyInt(const ItemOptions& options, int& value, float speed = 1.0f);
/** \brief 浮動小数点を編集する。値の意味上の検証は呼出側が担当する */
ItemResult PropertyFloat(const ItemOptions& options, float& value, float speed = 0.01f);
/** \brief UTF-8文字列を上限内で編集する。上限を超える既存値は読み取り専用で保持する */
ItemResult PropertyText(const ItemOptions& options, std::string& value, size_t maxBytes = 1024);
/** \brief 2〜4成分のVectorを編集する。Component固有の型は受け取らない */
ItemResult PropertyVector(const ItemOptions& options, std::span<float> values, float speed = 0.01f);
/** \brief QuaternionをXYZ Euler角（degree）として編集する */
ItemResult PropertyRotation(const ItemOptions& options, std::array<float, 4>& quaternion);
/** \brief RGBAの色を共通Styleで編集する */
ItemResult PropertyColor(const ItemOptions& options, std::array<float, 4>& color);
/** \brief 明示値を持つenumを編集する。未知値は数値表示し、勝手に変更しない */
ItemResult PropertyEnum(const ItemOptions& options, int& value, std::span<const EnumOption> choices);
}
