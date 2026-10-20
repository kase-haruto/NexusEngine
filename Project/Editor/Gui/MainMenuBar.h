#pragma once
#include "Panels/PanelRegistry.h"
#include "Editor/Services/EditorCommandRegistry.h"
#include "Editor/Services/LocalizationService.h"
#include <functional>
#include <vector>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * MainMenuBar
 * - Commandの実行可否とPanel表示をメニューへ反映する
 * - LevelやRendererの内部を直接操作しない
 *---------------------------------------------------------------------------------------*/
class MainMenuBar final {
public:
    struct Tool { std::string labelKey, command; }; //< 動的Tools登録
    /** \brief 将来のEditorツールを追加する */
    void RegisterTool(Tool tool) { tools_.push_back(std::move(tool)); }
    /** \brief 各メニューを描く。未登録commandは無効になる */
    void Draw(const LocalizationService& text, const EditorCommandRegistry& commands, PanelRegistry& panels,
        const std::vector<std::string>& recent, const std::function<void(const std::string&)>& openRecent,
        const std::function<std::vector<std::string>()>& listLevels = {}, const std::string& activeLevel = {});
private:
    std::vector<Tool> tools_; //< 外部登録したツール
};
}
