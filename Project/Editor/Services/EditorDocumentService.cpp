#include "EditorDocumentService.h"
#include "Runtime/Application/ApplicationPaths.h"

namespace NexusEngine {
void EditorDocumentService::Request(Action action, std::filesystem::path path) {
    if(pending_ != Action::None || saveAs_ || session_.IsPlaying()) return;
    if(action == Action::Open && !projectRoot_.empty()) {
        auto resolved = ResolveProjectPath(projectRoot_, path);
        if(!resolved) { error_ = resolved.error().GetMessageText(); return; }
        path = *resolved;
    }
    properties_.Commit(); error_.clear(); pending_ = action; path_ = std::move(path);
    if(IsDirty()) confirmation_ = true; else Perform();
}
void EditorDocumentService::Perform() {
    Result<void> result;
    switch(pending_) {
    case Action::New: result = session_.Create(); break;
    case Action::Open: result = session_.Open(path_); break;
    case Action::Exit: exit_ = true; break;
    default: break;
    }
    if(!result) error_ = result.error().GetMessageText();
    else if(pending_ == Action::New || pending_ == Action::Open) properties_.Reset(session_.GetLevel());
    pending_ = Action::None; confirmation_ = saveAs_ = false;
}
void EditorDocumentService::Discard() { confirmation_ = false; Perform(); }
////////////////////////////////////////////////////////////////////////////////
// 既存のEditor以外の利用者は従来の同期Scene切替を維持する
////////////////////////////////////////////////////////////////////////////////
void EditorDocumentService::EnableSavePreview() {
    deferSavedSwitch_ = true;
}

////////////////////////////////////////////////////////////////////////////////
// 保存の成功と、新しいSceneへの切替を別々のFrameで処理する
////////////////////////////////////////////////////////////////////////////////
bool EditorDocumentService::SavePreviewPending() const noexcept {
    return waitingForSavePreview_;
}

////////////////////////////////////////////////////////////////////////////////
// PreserveしていたSceneのFrameが確定した後に遷移する
////////////////////////////////////////////////////////////////////////////////
void EditorDocumentService::CompleteSavePreview() {
    if(waitingForSavePreview_) {
        waitingForSavePreview_ = false;
        Perform();
    }
}

////////////////////////////////////////////////////////////////////////////////
// 保存が完了した要求だけをEditorの次のFrameへ伝える
////////////////////////////////////////////////////////////////////////////////
uint64_t EditorDocumentService::SaveRevision() const noexcept {
    return saveRevision_;
}

bool EditorDocumentService::Save(std::optional<std::filesystem::path> path) {
    auto* document = session_.GetLevelDocument();
    if(!document || session_.IsPlaying()) return false;
    if(path && !projectRoot_.empty()) {
        auto resolved = ResolveProjectPath(projectRoot_, *path);
        if(!resolved) { error_ = resolved.error().GetMessageText(); return false; }
        path = *resolved;
    }
    properties_.Commit(); error_.clear();
    if(!path && !document->GetFilePath()) { confirmation_ = false; saveAs_ = true; return false; }
    auto result = path ? document->SaveAs(*path) : document->Save();
    if(!result) { error_ = result.error().GetMessageText(); return false; }
    ++saveRevision_;
    properties_.MarkSaved(); session_.MarkSaved(); confirmation_ = saveAs_ = false;
    if(deferSavedSwitch_ && (pending_ == Action::New || pending_ == Action::Open)) {
        waitingForSavePreview_ = true;
    } else if(pending_ != Action::None) {
        Perform();
    }
    return true;
}
}
