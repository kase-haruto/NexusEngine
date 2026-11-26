#pragma once
#include "Editor/Core/EditorSession.h"
#include "PropertyEditService.h"

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * EditorDocumentService
 * - New/Open/終了の未保存確認とSaveAs継続をUI非依存で管理する
 * - 保存失敗・取消では元Documentと保留操作を安全に維持する
 *---------------------------------------------------------------------------------------*/
class EditorDocumentService final {
public:
    enum class Action { None, New, Open, Exit };
    EditorDocumentService(EditorSession& session, PropertyEditService& properties) : session_(session), properties_(properties) {}
    /** \brief 起動ホストが解決したRootにLevel操作を限定する。未設定時は独立Documentとして扱う */
    void SetProjectRoot(std::filesystem::path root) { projectRoot_ = std::move(root); }
    /** \brief 未保存なら確認待ちにし、変更がなければ直ちに実行する */
    void Request(Action action, std::filesystem::path path = {});
    /** \brief 保留操作を取消し、Documentは保持する */
    void Cancel() { pending_ = Action::None; confirmation_ = saveAs_ = false; error_.clear(); }
    void Discard();
    /** \brief 保存後に開くSceneへの切替を、撮影Frameまで短時間だけ保留する */
    void EnableSavePreview();
    /** \brief 保存したSceneを描画する間、保留操作の状態を公開する */
    bool SavePreviewPending() const noexcept;
    /** \brief 保存したSceneを撮影済み、または待機期限に達した時点で切り替える */
    void CompleteSavePreview();
    /** \brief 保存先未指定ならSaveAsを要求。成功後だけ保留操作を実行する */
    bool Save(std::optional<std::filesystem::path> path = std::nullopt);
    bool IsDirty() const { return session_.IsDirty() || properties_.IsDirty(); }
    bool ConfirmationRequired() const { return confirmation_; }
    bool NeedsSaveAs() const { return saveAs_; }
    /** \brief 正常に保存した回数。ポップアップやCommandの経路に依存しない */
    uint64_t SaveRevision() const noexcept;
    bool ExitRequested() const { return exit_; }
    const std::string& Error() const { return error_; }
private:
    void Perform();
    EditorSession& session_; //< Document所有者
    PropertyEditService& properties_; //< 保存位置と未確定編集
    Action pending_ = Action::None; //< 確認後に継続する操作
    std::filesystem::path path_; //< Open先
    std::filesystem::path projectRoot_; //< Editorが開いているGameのRoot
    std::string error_; //< UIが通知する直近のエラー
    bool deferSavedSwitch_ = false; //< Project Previewが有効な場合の文書切替
    bool waitingForSavePreview_ = false; //< 旧Sceneを表示する短い猶予
    uint64_t saveRevision_ = 0; //< 最後に成功した保存の世代
    bool confirmation_ = false, saveAs_ = false, exit_ = false; //< 非同期UIの進行状態
};
}
