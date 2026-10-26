#pragma once
#include "EditorSession.h"
#include "Editor/Gui/Panels/PanelRegistry.h"
#include "Editor/Gui/PropertyDrawerRegistry.h"
#include "Editor/Gui/Properties/PropertyCategoryRegistry.h"
#include "Editor/Gui/DockingLayout.h"
#include "Editor/Gui/MainMenuBar.h"
#include "Editor/Services/PropertyEditService.h"
#include "Editor/Services/SceneEditService.h"
#include "Editor/Services/EditorDocumentService.h"
#include "Editor/Services/NotificationService.h"
#include "Editor/Gui/ViewportTools.h"
#include "Editor/Services/EditorSettings.h"
#include "Editor/Services/ConsoleService.h"
#include "Editor/Services/AssetCatalog.h"
#include "Editor/Gui/Widgets/AssetPropertyWidget.h"
#include "Editor/Runtime/ViewportSession.h"
#include "Foundation/Logging/BufferedLogSink.h"
#include <array>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * EditorWorkspace
 * - 既存EditorSessionと独立したPanel・Command・Settingsサービスを組み立てる
 * - RenderingとRuntime processの実装は所有しない
 *---------------------------------------------------------------------------------------*/
class EditorWorkspace final {
public:
    explicit EditorWorkspace(EditorSession& session);
    ~EditorWorkspace() noexcept;
    /** \brief ImGui初期化後にPanelとサービスを接続する */
    void Initialize(const std::filesystem::path& root, const std::vector<std::function<void(EditorWorkspace&)>>& extensions = {});
    /** \brief OSとメニューから同じ未保存確認を要求する */
    void RequestExit();
    /** \brief OSのDPI変更を次frameのStyleへ反映する */
    void SetDpiScale(float scale) { if(dpiScale_ != scale) { dpiScale_ = scale; settingsChanged_ = true; } }
    /** \brief ImGui NewFrame前にStyle・font・scaleの変更を反映する */
    void PrepareFrame();
    /** \brief 共通Menu、DockSpace、登録Panelを描画する */
    void Draw();
    /** \brief Context破棄前に設定と配置を保存する */
    void Shutdown();
    /** 編集値と表示状態だけを保存し、旧レイアウトのクロージャは持ち越さない。 */
    ReflectionValue CaptureForReinstancing();
    void ReleaseForReinstancing() noexcept;
    void RestoreAfterReinstancing(const ReflectionValue& snapshot);
    PanelRegistry& Panels() { return panels_; }
    const EditorSettingsStore::Editor& EditorPreferences() const { return settings_.editor; }
    MainMenuBar& Menu() { return menu_; }
    EditorCommandRegistry& Commands() { return commands_; }
    ViewportSession& Viewport() { return viewport_; }
    ViewportTools& ViewportEditor() { return viewportTools_; }
    AssetPropertyWidget& AssetEditor() { return assetEditor_; }
    /** \brief 起動時のEditor拡張から専門ページを登録する */
    PropertyCategoryRegistry& PropertyCategories() { return categories_; }
private:
    void RegisterCommands();
    void DrawFileDialog();
    void DrawUnsavedDialog();
    void ReportDocumentError();
    void OpenLevel(const std::filesystem::path& path);
    void RememberLevel(const std::filesystem::path& path);
    void Report(const Result<void>& result);
    void ApplyUserSettings();
    void SynchronizeDocument();
    EditorSession& session_; //< Application所有
    TypeRegistry types_; //< Runtime型情報
    PropertyDrawerRegistry drawers_; //< UI型別描画
    PropertyCategoryRegistry categories_; //< Panelより長寿命の専門ページ登録情報
    AssetPropertyWidget assetEditor_; //< Asset PickerとプレビューUI
    PropertyEditService properties_; //< 操作履歴
    SceneEditService sceneEdits_; //< 原子的な構造編集
    EditorDocumentService documents_; //< 保存・切替・終了の進行
    NotificationService notifications_; //< 操作結果の表示
    ViewportTools viewportTools_; //< 編集CameraとGizmo、CPU picking
    LocalizationService text_; //< UTF-8辞書
    EditorSettingsStore settings_; //< scope別設定
    EditorCommandRegistry commands_; //< Menuの操作窓口
    ConsoleService console_; //< 登録済みConsoleのみ
    PanelRegistry panels_; //< Panel所有
    MainMenuBar menu_; //< メニュー描画
    DockingLayout layout_; //< 配置永続化
    ViewportSession viewport_; //< 接続未設定ならDisconnected表示
    std::unique_ptr<AssetCatalog> assets_; //< ファイル閲覧サービス
    std::shared_ptr<BufferedLogBuffer> logs_; //< ログSinkと寿命共有
    ILogSink* logSink_ = nullptr; //< Logger所有、終了時に同期解除する識別参照
    std::vector<std::string> recent_; //< session内の最近使ったLevel
    std::array<char, 1024> filePath_{}; //< Path dialog入力
    std::string fileOperation_; //< open/saveAs要求
    std::string lastDocumentError_; //< 同じエラーの通知連打を防ぐ
    size_t generation_ = 0, revision_ = 0; //< document切替と編集の同期
    bool settingsChanged_ = true; //< 次frame先頭でUI設定を適用する
    float dpiScale_ = 1; //< 手動UI倍率と独立したOS倍率
    bool showAbout_ = false; //< About modal要求
    bool initialized_ = false; //< 部分初期化時の終了処理を保護
    PersistentId previousSelection_; //< 選択変更時にプロパティ操作を確定する
};
}
