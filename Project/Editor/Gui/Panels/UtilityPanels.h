#pragma once
#include "IEditorPanel.h"
#include "Editor/Services/ConsoleService.h"
#include "Editor/Services/AssetCatalog.h"
#include "Editor/Services/EditorSettings.h"
#include "Editor/Runtime/ViewportSession.h"
#include "Editor/Gui/ViewportTools.h"
#include "Foundation/Logging/BufferedLogSink.h"
#include <array>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * OutputLogPanel
 * - 上限付きログsnapshotの表示、検索、copyを担当する
 * - Loggerの配送threadでImGuiを呼ばない
 *---------------------------------------------------------------------------------------*/
class OutputLogPanel final : public IEditorPanel {
public:
    explicit OutputLogPanel(std::shared_ptr<BufferedLogBuffer> buffer) : buffer_(std::move(buffer)) {}
    std::string_view Id() const noexcept override { return "OutputLog"; }
    std::string_view TitleKey() const noexcept override { return "panel.log"; }
    void Draw(const EditorContext& context) override;
private:
    std::shared_ptr<BufferedLogBuffer> buffer_; //< Loggerと共有する値バッファ
    std::array<bool, 6> levels_{true, true, true, true, true, true}; //< レベルfilter
    std::string search_; //< 共通SearchBarのUTF-8検索入力
    bool autoScroll_ = true; //< 最下部に追従する
    uint64_t logRevision_ = 0; //< 最後に取り込んだログ番号
    std::vector<OwnedLogRecord> records_; //< 変更時だけ取得するsnapshot
    std::vector<std::string> lines_; //< filter適用済みの整形行
    std::string filterKey_; //< 検索・level・言語のcache識別子
};
/*-----------------------------------------------------------------------------------------
 * TerminalPanel
 * - 登録Consoleの入力と結果表示のみを担当する
 * - 履歴と実行・補完はConsoleServiceへ委譲する
 *---------------------------------------------------------------------------------------*/
class TerminalPanel final : public IEditorPanel {
public:
    explicit TerminalPanel(ConsoleService& console) : console_(console) {}
    std::string_view Id() const noexcept override { return "Terminal"; }
    std::string_view TitleKey() const noexcept override { return "panel.terminal"; }
    void Draw(const EditorContext& context) override;
private:
    void UpdateSuggestions(std::string_view input);
    ConsoleService& console_; //< host所有
    std::array<char, 512> input_{}; //< 入力上限
    std::deque<std::string> output_; //< 最大256出力
    int historyIndex_ = -1; //< -1は新規入力
    bool scrollToBottom_ = false;
    bool focusInput_ = false;
    std::vector<std::string> suggestions_;
    std::string suggestionPrefix_, pendingCompletion_;
    int suggestionIndex_ = 0;
    bool suggestionsOpen_ = false, scrollToSuggestion_ = false;
};
/*-----------------------------------------------------------------------------------------
 * ContentBrowserPanel
 * - フォルダ・ファイルカタログを表示する
 * - Asset importやGPU uploadは担当しない
 *---------------------------------------------------------------------------------------*/
class ContentBrowserPanel final : public IEditorPanel {
public:
    explicit ContentBrowserPanel(AssetCatalog& assets, std::function<void(const std::filesystem::path&)> open)
        : assets_(assets), open_(std::move(open)) {}
    std::string_view Id() const noexcept override { return "ContentBrowser"; }
    std::string_view TitleKey() const noexcept override { return "panel.content"; }
    void Draw(const EditorContext& context) override;
private:
    AssetCatalog& assets_; //< host所有
    std::function<void(const std::filesystem::path&)> open_; //< ダブルクリックの拡張ポイント
    std::string search_; //< 共通SearchBarのUTF-8検索入力
};
/*-----------------------------------------------------------------------------------------
 * PreferencesPanel
 * - User設定UIのみを担当する。保存処理はStore/hostへ委譲する
 *---------------------------------------------------------------------------------------*/
class PreferencesPanel final : public IEditorPanel {
public:
    PreferencesPanel(EditorSettingsStore& settings, std::function<void()> changed) : settings_(settings), changed_(std::move(changed)) {}
    std::string_view Id() const noexcept override { return "Preferences"; }
    std::string_view TitleKey() const noexcept override { return "edit.preferences"; }
    void Draw(const EditorContext& context) override;
private:
    EditorSettingsStore& settings_; //< host所有
    std::function<void()> changed_; //< 次frameのstyle適用と保存要求
};
/*-----------------------------------------------------------------------------------------
 * ViewportPanel
 * - 表示領域、画像、入力とResize通知のみを管理する
 * - Process起動、通信実装、GPU資源生成は担当しない
 *---------------------------------------------------------------------------------------*/
class ViewportPanel final : public IEditorPanel {
public:
    ViewportPanel(ViewportSession& session, ViewportTools& tools) : session_(session), tools_(tools) {}
    std::string_view Id() const noexcept override { return "Viewport"; }
    std::string_view TitleKey() const noexcept override { return "panel.viewport"; }
    void Draw(const EditorContext& context) override;
private:
    ViewportSession& session_; //< host所有
    ViewportTools& tools_; //< host所有。GPUへ直接アクセスしない
};
}
