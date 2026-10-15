#pragma once
#include "IEditorPanel.h"
#include <memory>
#include <vector>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * PanelRegistry
 * - Panelの所有、表示状態、安定Window IDを管理する
 * - 個々のPanelの内容やRuntime操作を知らない
 *---------------------------------------------------------------------------------------*/
class PanelRegistry final {
public:
    struct Entry {
        std::unique_ptr<IEditorPanel> panel; //< 登録Panelの唯一の所有者
        bool isOpen = true; //< 保存可能な表示状態
    };
    /** \brief 重複IDを拒否してPanelを登録する */
    bool Register(std::unique_ptr<IEditorPanel> panel, bool open = true);
    /** \brief 開いているPanelのみ描画する */
    void Draw(const EditorContext& context);
    auto& Entries() { return entries_; }
    const auto& Entries() const { return entries_; }
    /** \brief 安定IDでPanelを開く */
    void Open(std::string_view id);
private:
    std::vector<Entry> entries_; //< 登録順のPanel
};
}
