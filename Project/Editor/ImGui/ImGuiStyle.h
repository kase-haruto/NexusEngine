#pragma once
#include "Editor/Gui/Core/GuiTheme.h"

namespace NexusEngine::UI {
/*-----------------------------------------------------------------------------------------
 * EditorStyleOptions
 * - 既存のEditor配色を基準にした表示オプション
 * - ImGui固有型と設定ファイルの永続化は公開しない
 *---------------------------------------------------------------------------------------*/
struct EditorStyleOptions {
    bool light = false; //< 同じアクセントを維持した明色派生
    float fontSize = 18.0f; //< 論理フォントサイズ
    float scale = 1.0f; //< UI寸法とフォントの倍率
};
/**
 * \brief 既存の黒基調・Orange Accentを唯一の基準として全体へ適用する
 * \param options 設定から読み込んだ表示オプション
 * \note Context生成後、Widget描画前に呼ぶ。再適用しても寸法を重複拡大しない
 */
void ApplyEditorStyle(const EditorStyleOptions& options = {}) noexcept;
/** \brief 適用済みStyleを独自Widget用の意味ベースThemeへ変換する */
Theme CurrentEditorTheme() noexcept;
}
