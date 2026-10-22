#pragma once
#include "Editor/Gui/Panels/IEditorPanel.h"
#include "ThirdParty/imgui/imgui.h"
#include <functional>
#include <string>
#include <vector>

namespace NexusEngine {
struct PropertyCategory {
    std::string id, labelKey; //< 言語非依存のIDと翻訳キー
    int order = 0; //< 同順位ではID順
    std::function<bool(const EditorContext&)> isAvailable; //< 毎frame選択から判定する
    std::function<void(const EditorContext&)> drawPage; //< ページが必要な状態はcallback側で所有する
    std::function<void(ImDrawList&, ImVec2, float, ImU32)> drawIcon; //< 公開DrawList APIによる小アイコン
    std::vector<std::string> componentTypes; //< 専用ページを持つ型ID。「その他」の自動分類に使用する
};
/*-----------------------------------------------------------------------------------------
 * PropertyCategoryRegistry
 * - 専門ページの登録情報と表示順を保持するEditor専用の拡張点
 * - ページ生成FactoryやRuntimeの型情報・選択状態は所有しない
 *---------------------------------------------------------------------------------------*/
class PropertyCategoryRegistry final {
public:
    /** \brief 重複ID・不完全な登録を拒否する。Workspaceの拡張初期化時に呼ぶ */
    bool Register(PropertyCategory category);
    /** \brief 現在の選択に利用可能なカテゴリを登録順で返す。ポインタは次の登録まで有効 */
    std::vector<const PropertyCategory*> Available(const EditorContext& context) const;
    /** \brief 指定Componentに専用カテゴリが登録されているかを調べる */
    bool HasCategoryFor(std::string_view type, const EditorContext& context) const;
private:
    std::vector<PropertyCategory> categories_; //< Workspace寿命内の登録情報
};
/** \brief 組み込みカテゴリと未分類Component用ページを登録する */
void RegisterPropertyPages(PropertyCategoryRegistry& registry);
}
