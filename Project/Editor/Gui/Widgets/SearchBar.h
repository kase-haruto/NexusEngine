#pragma once

#include <string>

namespace NexusEngine::UI {
    class Context;

    /** @brief 虫眼鏡付き検索入力。検索処理と文字列の所有は呼び出し側が担当する。
     * @param label 現在のImGui IDスコープで一意な識別子。表示ラベルには使用しない。
     * @param text UTF-8入力文字列。外部からの変更も入力中に反映する。
     * @param hint 空文字列時の表示。nullptrは空のHintとして扱う。
     * @param width ピクセル幅。0以下は現在のレイアウトの利用可能幅。
     * @return この呼び出しの入力操作によって文字列が変わった場合のみtrue。
     * @pre ImGuiフレームとWindowが開始済み。labelは空でないこと。
     */
    bool SearchBar(const char* label, std::string& text,
                   const char* hint = "Search...", float width = 0.0f);

    /** @brief ContextのThemeを使用するSearchBar。その他の契約は通常版と同じ。 */
    bool SearchBar(Context& context, const char* label, std::string& text,
                   const char* hint = "Search...", float width = 0.0f);
}
