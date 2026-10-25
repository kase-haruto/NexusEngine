#pragma once
#include "IEditorPanel.h"
#include <memory>
#include <functional>
#include <string>
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
        std::string id, titleKey; //< 未生成でもWindowメニューに出せるmetadata
        std::function<std::unique_ptr<IEditorPanel>()> factory; //< サービスを借用する遅延Factory
        std::string_view Id() const { return id; }
        std::string_view TitleKey() const { return titleKey; }
    };
    /** \brief 重複IDを拒否してPanelを登録する */
    bool Register(std::unique_ptr<IEditorPanel> panel, bool open = true);
    /** \brief 初めて表示する時に生成する。描画callback中の登録は拒否する */
    bool RegisterFactory(std::string id, std::string titleKey, std::function<std::unique_ptr<IEditorPanel>()> factory, bool open = true);
    /** \brief 描画中は解除を予約し、End後に破棄する */
    bool Unregister(std::string_view id);
    /** \brief サービス破棄前に全PanelとFactoryを解放する。描画中はEnd後へ延期する */
    void Clear();
    /** \brief 開いているPanelのみ描画する */
    void Draw(const EditorContext& context, bool disableEditing = false);
    auto& Entries() { return entries_; }
    const auto& Entries() const { return entries_; }
    /** \brief 安定IDでPanelを開く */
    void Open(std::string_view id);
private:
    std::vector<Entry> entries_; //< 登録順のPanel
    std::vector<std::string> removed_; //< callbackから要求された解除
    bool drawing_ = false; //< vectorと描画中Panelの寿命を保護する
};
}
