#pragma once
#include <deque>
#include <exception>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * ConsoleService
 * - 登録済みエンジンコマンド、履歴、補完を管理する
 * - OS shellを実行しない。登録と実行はUIスレッドで行う
 *---------------------------------------------------------------------------------------*/
class ConsoleService final {
public:
    struct Output {
        std::string text; //< コマンド出力または翻訳キー
        bool translate = false; //< システムエラーだけを翻訳する
        Output(std::string value = {}, bool localized = false) : text(std::move(value)), translate(localized) {}
        Output(const char* value) : text(value) {}
    };
    using Handler = std::function<Output(std::string_view)>;
    /** \brief 重複を拒否して内部コマンドを登録する */
    bool Register(std::string name, Handler handler) { return commands_.emplace(std::move(name), std::move(handler)).second; }
    /** \brief 入力をコマンド名と引数へ分けて安全に実行する */
    Output Execute(std::string_view line) {
        if(line.empty()) return {};
        history_.emplace_back(line);
        if(history_.size() > 128) history_.pop_front();
        const auto start = line.find_first_not_of(" \t");
        if(start == line.npos) return {};
        line.remove_prefix(start);
        if(line.starts_with('/')) line.remove_prefix(1);
        if(line.empty()) return {};
        const auto end = line.find_first_of(" \t");
        auto it = commands_.find(std::string(line.substr(0, end)));
        if(it == commands_.end()) return {"console.unknown", true};
        try { return it->second(end == line.npos ? std::string_view{} : line.substr(end + 1)); }
        catch(const std::exception& e) { return {e.what()}; }
        catch(...) { return {"console.failed", true}; }
    }
    /** \brief 補完候補を返す。将来引数Completerを追加可能 */
    std::vector<std::string> Complete(std::string_view prefix) const {
        if(prefix.starts_with('/')) prefix.remove_prefix(1);
        std::vector<std::string> result;
        for(const auto& [name, handler] : commands_) if(name.starts_with(prefix)) result.push_back(name);
        return result;
    }
    const std::deque<std::string>& History() const { return history_; }
private:
    std::map<std::string, Handler> commands_; //< 登録関数のみ実行する
    std::deque<std::string> history_; //< 最大128入力
};
}
