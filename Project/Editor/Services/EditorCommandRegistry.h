#pragma once
#include <functional>
#include <map>
#include <string>
#include <string_view>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * EditorCommandRegistry
 * - Menuやショートカットからの操作要求と実行可否を解決する
 * - Menu描画側へRuntime操作を漏らさない
 *---------------------------------------------------------------------------------------*/
class EditorCommandRegistry final {
public:
    struct Command {
        std::function<void()> execute; //< composition rootで接続する操作
        std::function<bool()> canExecute; //< 未対応や現在状態を反映する
        int key = 0; //< GUI adapterが解釈するキー値。0は割当なし
        bool ctrl = false, shift = false, alt = false; //< 完全一致する修飾キー
        std::string shortcut; //< メニュー・Tooltipと共有する割当表示
    };
    /** \brief 安定IDでコマンドを追加する */
    bool Register(std::string id, Command command) {
        return !id.empty() && command.execute && commands_.emplace(std::move(id), std::move(command)).second;
    }
    /** \brief 同じ割当の競合を拒否してShortcutを接続する */
    bool Bind(std::string_view id, int key, bool ctrl, bool shift, bool alt, std::string label) {
        auto it = commands_.find(id);
        if(it == commands_.end() || key == 0) return false;
        for(const auto& [otherId, other] : commands_)
            if(otherId != id && other.key == key && other.ctrl == ctrl && other.shift == shift && other.alt == alt) return false;
        it->second.key = key; it->second.ctrl = ctrl; it->second.shift = shift; it->second.alt = alt;
        it->second.shortcut = std::move(label); return true;
    }
    const char* Shortcut(std::string_view id) const {
        auto it = commands_.find(id); return it == commands_.end() ? "" : it->second.shortcut.c_str();
    }
    /** \brief adapterから現在のキー入力を照会し、共通操作を実行する */
    void Dispatch(bool ctrl, bool shift, bool alt, const std::function<bool(int)>& pressed) const {
        for(const auto& [id, command] : commands_)
            if(command.key && command.ctrl == ctrl && command.shift == shift && command.alt == alt && pressed(command.key)) Execute(id);
    }
    bool CanExecute(std::string_view id) const {
        auto it = commands_.find(id);
        return it != commands_.end() && it->second.execute && (!it->second.canExecute || it->second.canExecute());
    }
    /** \brief 実行可能性を再検査して操作要求を実行する */
    bool Execute(std::string_view id) const {
        if(!CanExecute(id)) return false;
        commands_.find(id)->second.execute(); return true;
    }
private:
    std::map<std::string, Command, std::less<>> commands_; //< UI文字列と独立したコマンド集合
};
}
