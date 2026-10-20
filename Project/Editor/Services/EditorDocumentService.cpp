#include "EditorDocumentService.h"

namespace NexusEngine {
void EditorDocumentService::Request(Action action, std::filesystem::path path) {
    if(pending_ != Action::None || saveAs_ || session_.IsPlaying()) return;
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
bool EditorDocumentService::Save(std::optional<std::filesystem::path> path) {
    auto* document = session_.GetLevelDocument();
    if(!document || session_.IsPlaying()) return false;
    properties_.Commit(); error_.clear();
    if(!path && !document->GetFilePath()) { confirmation_ = false; saveAs_ = true; return false; }
    auto result = path ? document->SaveAs(*path) : document->Save();
    if(!result) { error_ = result.error().GetMessageText(); return false; }
    properties_.MarkSaved(); session_.MarkSaved(); confirmation_ = saveAs_ = false;
    if(pending_ != Action::None) Perform();
    return true;
}
}
