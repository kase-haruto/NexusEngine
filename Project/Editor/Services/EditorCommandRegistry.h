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
    };
    /** \brief 安定IDでコマンドを追加する */
    bool Register(std::string id, Command command) { return commands_.emplace(std::move(id), std::move(command)).second; }
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
