#include "EditorWorkspace.h"
#include "Editor/ImGui/ImGuiStyle.h"
#include "Editor/Gui/EditorChrome.h"
#include "Editor/Gui/Panels/OutlinerPanel.h"
#include "Editor/Gui/Panels/DetailsPanel.h"
#include "Editor/Gui/Panels/UtilityPanels.h"
#include "Foundation/Logging/Logger.h"
#include "Foundation/Utility/String/ConvertString.h"
#include "ThirdParty/imgui/imgui.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace NexusEngine {
EditorWorkspace::EditorWorkspace(EditorSession& session)
    : session_(session), properties_(types_), sceneEdits_(session, properties_), documents_(session, properties_), viewportTools_(session, properties_) {}
EditorWorkspace::~EditorWorkspace() noexcept {
    // 部分初期化時も、Panel/Factoryが借用するAsset・Viewport・編集サービスより先に破棄する。
    panels_.Clear(); properties_.Reset(nullptr);
    if(logSink_) Logger::Get().RemoveSink(logSink_);
}
void EditorWorkspace::Initialize(const std::filesystem::path& root, const std::vector<std::function<void(EditorWorkspace&)>>& extensions) {
    settings_.Load(root); recent_ = settings_.user.recentLevels;
    // 旧Propertiesだけを開いていた利用者も、統合先のDetailsへ移行する。
    if(auto legacy = settings_.user.panels.find("Properties"); legacy != settings_.user.panels.end()) {
        const auto details = settings_.user.panels.find("Details");
        const bool detailsWasOpen = details == settings_.user.panels.end() || details->second;
        settings_.user.panels["Details"] = detailsWasOpen || legacy->second;
        settings_.user.panels.erase(legacy);
    }
    if(!text_.Load(root / "Resources/Editor/Localization")) NEXUS_LOG_WARNING("Editor", "Localization dictionary unavailable.");
    text_.SetLanguage(settings_.user.language);
    RegisterSceneTypes(types_); drawers_.RegisterDefaults();
    RegisterPropertyPages(categories_);
    properties_.Reset(session_.GetLevel()); generation_ = session_.Generation();
    assets_ = std::make_unique<AssetCatalog>(root / std::filesystem::path{ConvertString(settings_.project.assetPath)});
    logs_ = std::make_shared<BufferedLogBuffer>();
    auto sink = std::make_unique<BufferedLogSink>(logs_); logSink_ = sink.get();
    if(!Logger::Get().AddSink(std::move(sink))) logSink_ = nullptr;
    panels_.RegisterFactory("Outliner", "panel.outliner", [] { return std::make_unique<OutlinerPanel>(); });
    panels_.RegisterFactory("Details", "panel.details", [this] { return std::make_unique<DetailsPanel>(categories_); });
    panels_.RegisterFactory("Viewport", "panel.viewport", [this] { return std::make_unique<ViewportPanel>(viewport_, viewportTools_); }, settings_.editor.viewport);
    panels_.RegisterFactory("OutputLog", "panel.log", [this] { return std::make_unique<OutputLogPanel>(logs_); });
    panels_.RegisterFactory("Terminal", "panel.terminal", [this] { return std::make_unique<TerminalPanel>(console_); });
    panels_.RegisterFactory("ContentBrowser", "panel.content", [this] {
        return std::make_unique<ContentBrowserPanel>(*assets_, [this](const auto& path) {
            if(path.extension() == ".json" && path.parent_path().filename() == "Levels") OpenLevel(path);
            else notifications_.Push(AssetCatalog::Utf8(path));
        });
    });
    panels_.RegisterFactory("Preferences", "edit.preferences", [this] {
        return std::make_unique<PreferencesPanel>(settings_, [this] { settingsChanged_ = true; });
    }, false);
    RegisterCommands();
    // 新規Panel/Commandは外部の機能登録から追加でき、中央の具象型一覧を変更しない。
    for(const auto& extension : extensions) extension(*this);
    for(auto& entry : panels_.Entries()) {
        auto it = settings_.user.panels.find(std::string(entry.Id()));
        if(it != settings_.user.panels.end()) entry.isOpen = it->second;
    }
    console_.Register("help", [this](auto) {
        std::string result; for(const auto& name : console_.Complete({})) result += name + '\n'; return result;
    });
    console_.Register("echo", [](auto argument) { return std::string(argument); });
    console_.Register("log.clear", [this](auto) { logs_->Clear(); return std::string{}; });
    console_.Register("editor.undo", [this](auto) { return ConsoleService::Output{commands_.Execute("edit.undo") ? "" : "console.unavailable", true}; });
    console_.Register("editor.redo", [this](auto) { return ConsoleService::Output{commands_.Execute("edit.redo") ? "" : "console.unavailable", true}; });
    // FNV-1aでProjectの正規化パスを識別し、他Projectの配置を上書きしない。
    std::error_code pathError;
    const auto projectPath = AssetCatalog::Utf8(std::filesystem::weakly_canonical(root,pathError));
    uint64_t projectKey = 14695981039346656037ull;
    for(unsigned char byte : projectPath) { projectKey ^= byte; projectKey *= 1099511628211ull; }
    layout_.Initialize(settings_.UserDirectory() / "Workspaces" / std::to_string(projectKey),settings_.user.workspace);
    initialized_ = true;
    NEXUS_LOG_INFO("Editor", "Editor workspace initialized.");
}
void EditorWorkspace::Report(const Result<void>& result) {
    if(!result) { NEXUS_LOG_ERROR("Editor", result.error().GetMessageText()); notifications_.Push(result.error().GetMessageText(), true); }
}
void EditorWorkspace::ReportDocumentError() {
    if(documents_.Error().empty()) { lastDocumentError_.clear(); return; }
    if(documents_.Error() != lastDocumentError_) {
        lastDocumentError_ = documents_.Error(); notifications_.Push(lastDocumentError_, true); NEXUS_LOG_ERROR("Editor", lastDocumentError_);
    }
}
void EditorWorkspace::SynchronizeDocument() {
    if(generation_ == session_.Generation()) return;
    properties_.Reset(session_.GetLevel()); generation_ = session_.Generation();
    previousSelection_ = {}; revision_ = properties_.Revision();
}
void EditorWorkspace::OpenLevel(const std::filesystem::path& path) {
    documents_.Request(EditorDocumentService::Action::Open, path);
    SynchronizeDocument(); ReportDocumentError();
    if(auto* document = session_.GetLevelDocument(); document && document->GetFilePath() == path) RememberLevel(path);
}
void EditorWorkspace::RememberLevel(const std::filesystem::path& path) {
    auto value = AssetCatalog::Utf8(path); std::erase(recent_, value); recent_.insert(recent_.begin(), value);
    if(recent_.size() > 10) recent_.resize(10);
    settings_.user.recentLevels = recent_;
    if(!settings_.SaveUser()) notifications_.Push(text_.Text("settings.saveFailed"), true);
}
void EditorWorkspace::RequestExit() {
    session_.StopPlay(); documents_.Request(EditorDocumentService::Action::Exit);
}
void EditorWorkspace::RegisterCommands() {
    const auto editing = [this] { return !session_.IsPlaying() && !documents_.ConfirmationRequired() && !documents_.NeedsSaveAs(); };
    const auto sceneResult = [this](bool result) { if(!result) notifications_.Push(text_.Text("edit.rejected"), true); };
    commands_.Register("file.new", {[this] { documents_.Request(EditorDocumentService::Action::New); }, editing});
    commands_.Register("file.open", {[this] { fileOperation_ = "open"; filePath_.fill(0); }, editing});
    commands_.Register("file.save", {[this] {
        if(documents_.Save()) notifications_.Push(text_.Text("file.saved"));
        ReportDocumentError();
    }, [this, editing] { return editing() && session_.GetLevelDocument(); }});
    commands_.Register("file.saveAs", {[this] { fileOperation_ = "saveAs"; filePath_.fill(0); }, editing});
    commands_.Register("file.exit", {[this] { RequestExit(); }, {}});
    commands_.Register("edit.undo", {[this] { if(!properties_.Undo()) notifications_.Push(text_.Text("edit.rejected"), true); },
        [this, editing] { return editing() && properties_.CanUndo(); }});
    commands_.Register("edit.redo", {[this] { if(!properties_.Redo()) notifications_.Push(text_.Text("edit.rejected"), true); },
        [this, editing] { return editing() && properties_.CanRedo(); }});
    commands_.Register("edit.delete", {[this, sceneResult] { sceneResult(sceneEdits_.DeleteSelection()); },
        [this, editing] { return editing() && session_.GetSelectedEntity().IsAlive(); }});
    commands_.Register("scene.entity", {[this, sceneResult] { sceneResult(sceneEdits_.Create("Entity")); }, editing});
    commands_.Register("scene.camera", {[this, sceneResult] { sceneResult(sceneEdits_.Create("Camera")); }, editing});
    commands_.Register("scene.model", {[this, sceneResult] { sceneResult(sceneEdits_.Create("Model")); }, editing});
    commands_.Register("scene.light", {[this, sceneResult] { sceneResult(sceneEdits_.Create("DirectionalLight")); }, editing});
    commands_.Register("play.start", {[this, sceneResult] { properties_.Commit(); sceneResult(session_.StartPlay()); }, editing});
    commands_.Register("play.pause", {[this] { session_.TogglePause(); }, [this] { return session_.IsPlaying(); }});
    commands_.Register("play.stop", {[this] { session_.StopPlay(); }, [this] { return session_.IsPlaying(); }});
    commands_.Register("edit.preferences", {[this] { panels_.Open("Preferences"); }, {}});
    commands_.Register("focus.details", {[this] { panels_.Open("Details"); ImGui::SetWindowFocus(text_.Label("panel.details", "Details").c_str()); }, {}});
    commands_.Register("window.reset", {[this] { layout_.Reset(); }, {}});
    for(const std::string name : {"General", "Lighting", "Animation"}) {
        commands_.Register("workspace." + name, {[this, name] {
            layout_.SwitchProfile(name,settings_.user.autoSaveLayout); settings_.user.workspace = name;
            if(!settings_.SaveUser()) notifications_.Push(text_.Text("settings.saveFailed"),true);
        }, {}});
    }
    commands_.Register("viewport.focus", {[this] { viewportTools_.FocusSelection(); }, [this, editing] { return editing() && session_.GetSelectedEntity().IsAlive(); }});
    commands_.Register("help.about", {[this] { showAbout_ = true; }, {}});
    commands_.Bind("file.new", ImGuiKey_N, true, false, false, "Ctrl+N");
    commands_.Bind("file.open", ImGuiKey_O, true, false, false, "Ctrl+O");
    commands_.Bind("file.save", ImGuiKey_S, true, false, false, "Ctrl+S");
    commands_.Bind("file.saveAs", ImGuiKey_S, true, true, false, "Ctrl+Shift+S");
    commands_.Bind("edit.undo", ImGuiKey_Z, true, false, false, "Ctrl+Z");
    commands_.Bind("edit.redo", ImGuiKey_Y, true, false, false, "Ctrl+Y");
    commands_.Bind("edit.delete", ImGuiKey_Delete, false, false, false, "Delete");
    commands_.Bind("play.start", ImGuiKey_F5, false, false, false, "F5");
    commands_.Bind("play.stop", ImGuiKey_F5, false, true, false, "Shift+F5");
    commands_.Bind("viewport.focus", ImGuiKey_F, false, false, false, "F");
}
void EditorWorkspace::ApplyUserSettings() {
    text_.SetLanguage(settings_.user.language);
    UI::ApplyEditorStyle({settings_.user.theme == "Light", settings_.user.fontSize, settings_.user.uiScale * dpiScale_});
    if(!settings_.SaveUser()) notifications_.Push(text_.Text("settings.saveFailed"), true);
    settingsChanged_ = false;
}
void EditorWorkspace::DrawUnsavedDialog() {
    if(documents_.ConfirmationRequired()) ImGui::OpenPopup("###Unsaved");
    if(ImGui::BeginPopupModal(text_.Label("file.unsaved", "Unsaved").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(text_.Text("file.unsavedBody").c_str());
        if(ImGui::Button(text_.Label("file.save", "save").c_str())) {
            if(documents_.Save() || documents_.NeedsSaveAs()) ImGui::CloseCurrentPopup();
            ReportDocumentError();
        }
        ImGui::SameLine();
        if(ImGui::Button(text_.Label("file.discard", "discard").c_str())) { documents_.Discard(); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if(ImGui::Button(text_.Label("cancel", "cancel").c_str())) { documents_.Cancel(); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}
void EditorWorkspace::DrawFileDialog() {
    if(documents_.NeedsSaveAs() && fileOperation_.empty()) { fileOperation_ = "saveAs"; filePath_.fill(0); }
    if(!fileOperation_.empty()) ImGui::OpenPopup("###LevelPath");
    if(ImGui::BeginPopupModal(text_.Label("file.path", "LevelPath").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText(text_.Label("file.path", "path").c_str(), filePath_.data(), filePath_.size());
        if(ImGui::Button(text_.Label("apply", "apply").c_str()) && filePath_[0]) {
            auto path = std::filesystem::path{ConvertString(std::string{filePath_.data()})};
            bool complete = true;
            if(fileOperation_ == "open") OpenLevel(path);
            else {
                complete = documents_.Save(path); ReportDocumentError();
                if(complete) { RememberLevel(path); notifications_.Push(text_.Text("file.saved")); }
            }
            if(complete) { fileOperation_.clear(); ImGui::CloseCurrentPopup(); }
        }
        if(!documents_.Error().empty()) ImGui::TextWrapped("%s", documents_.Error().c_str());
        ImGui::SameLine();
        if(ImGui::Button(text_.Label("cancel", "cancel").c_str())) { fileOperation_.clear(); documents_.Cancel(); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}
void EditorWorkspace::PrepareFrame() { if(initialized_ && settingsChanged_) ApplyUserSettings(); }
void EditorWorkspace::Draw() {
    if(!initialized_) return;
    SynchronizeDocument();
    std::string activeLevel;
    auto* document = session_.GetLevelDocument();
    if(document && document->GetFilePath()) {
        std::error_code error; activeLevel = AssetCatalog::Utf8(std::filesystem::weakly_canonical(*document->GetFilePath(), error));
    }
    menu_.Draw(text_, commands_, panels_, recent_, [this](const auto& path) { OpenLevel(std::filesystem::path{ConvertString(path)}); },
        [this] {
            std::vector<std::string> levels;
            for(const auto& entry : *assets_->Snapshot(assets_->Root() / "Levels"))
                if(!entry.directory && entry.typeKey == "asset.level") levels.push_back(AssetCatalog::Utf8(entry.path));
            return levels;
        }, activeLevel);
    DrawEditorToolbar(text_, commands_, activeLevel.empty() ? text_.Text("file.untitled") : activeLevel,
        documents_.IsDirty(), session_.IsPlaying(), session_.IsPaused());
    layout_.Draw();
    const EditorContext context{session_, session_.Selection(), properties_, types_, drawers_, text_, commands_, sceneEdits_, &assetEditor_};
    ImGui::BeginDisabled(session_.IsPlaying()); panels_.Draw(context); ImGui::EndDisabled();
    viewportTools_.CompleteInteraction();
    if(previousSelection_ != session_.GetSelection()) { properties_.Commit(); previousSelection_ = session_.GetSelection(); }
    if(!ImGui::IsAnyItemActive() && !viewportTools_.IsManipulating()) properties_.Commit();
    if(ImGui::IsKeyPressed(ImGuiKey_Escape) && ImGui::IsAnyItemActive()) properties_.Cancel();
    DrawUnsavedDialog(); DrawFileDialog(); SynchronizeDocument(); ReportDocumentError();
    const auto& io = ImGui::GetIO();
    if(!io.WantTextInput && !ImGui::IsAnyItemActive() && !viewportTools_.IsManipulating() && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
        commands_.Dispatch(io.KeyCtrl, io.KeyShift, io.KeyAlt, [](int key) { return ImGui::IsKeyPressed(static_cast<ImGuiKey>(key), false); });
    if(showAbout_) { ImGui::OpenPopup("###About"); showAbout_ = false; }
    if(ImGui::BeginPopupModal(text_.Label("help.about", "About").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(text_.Text("about.body").c_str());
        if(ImGui::Button(text_.Label("close", "close").c_str())) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    DrawEditorNotifications(notifications_);
    if(documents_.ExitRequested()) PostQuitMessage(0);
}
void EditorWorkspace::Shutdown() {
    if(!initialized_) return;
    properties_.Commit();
    for(const auto& entry : panels_.Entries()) settings_.user.panels[std::string(entry.Id())] = entry.isOpen;
    if(!settings_.SaveUser()) NEXUS_LOG_WARNING("Editor", "Failed to save user settings.");
    layout_.Save(settings_.user.autoSaveLayout); panels_.Clear(); viewport_.Detach(); properties_.Reset(nullptr);
    if(logSink_) { Logger::Get().RemoveSink(logSink_); logSink_ = nullptr; }
    initialized_ = false;
}
}
