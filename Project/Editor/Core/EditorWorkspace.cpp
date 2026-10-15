#include "EditorWorkspace.h"
#include "Editor/ImGui/ImGuiStyle.h"
#include "Editor/Gui/Panels/OutlinerPanel.h"
#include "Editor/Gui/Panels/DetailsPanel.h"
#include "Editor/Gui/Panels/UtilityPanels.h"
#include "Foundation/Logging/Logger.h"
#include "Foundation/Utility/String/ConvertString.h"
#include "ThirdParty/imgui/imgui.h"
#include <cstdio>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace NexusEngine {
EditorWorkspace::EditorWorkspace(EditorSession& session) : session_(session), properties_(types_) {}
EditorWorkspace::~EditorWorkspace() noexcept {
    // 部分初期化失敗でもLoggerに画面のバッファを残さない。ImGuiには触れない。
    if(logSink_) Logger::Get().RemoveSink(logSink_);
}
void EditorWorkspace::Initialize(const std::filesystem::path& root) {
    settings_.Load(root);
    recent_ = settings_.user.recentLevels;
    if(!text_.Load(root / "Resources/Editor/Localization")) NEXUS_LOG_WARNING("Editor", "Localization dictionary unavailable; displaying fallback keys.");
    text_.SetLanguage(settings_.user.language);
    RegisterSceneTypes(types_); drawers_.RegisterDefaults();
    properties_.Reset(session_.GetLevel()); generation_ = session_.Generation();
    assets_ = std::make_unique<AssetCatalog>(root / std::filesystem::path{ConvertString(settings_.project.assetPath)});
    logs_ = std::make_shared<BufferedLogBuffer>();
    auto sink = std::make_unique<BufferedLogSink>(logs_);
    logSink_ = sink.get();
    if(!Logger::Get().AddSink(std::move(sink))) {
        logSink_ = nullptr;
        NEXUS_LOG_WARNING("Editor", "Output log sink registration failed.");
    }
    panels_.Register(std::make_unique<OutlinerPanel>());
    panels_.Register(std::make_unique<DetailsPanel>());
    panels_.Register(std::make_unique<ViewportPanel>(viewport_), settings_.editor.viewport);
    panels_.Register(std::make_unique<OutputLogPanel>(logs_));
    panels_.Register(std::make_unique<TerminalPanel>(console_));
    panels_.Register(std::make_unique<ContentBrowserPanel>(*assets_, [this](const auto& path) {
        if(path.extension() == ".json" && path.parent_path().filename() == "Levels") OpenLevel(path);
        else NEXUS_LOG_INFO("ContentBrowser", AssetCatalog::Utf8(path));
    }));
    panels_.Register(std::make_unique<PreferencesPanel>(settings_, [this] { settingsChanged_ = true; }), false);
    for(auto& entry : panels_.Entries()) {
        auto it = settings_.user.panels.find(std::string(entry.panel->Id()));
        if(it != settings_.user.panels.end()) entry.isOpen = it->second;
    }
    RegisterCommands();
    console_.Register("help", [this](auto) {
        std::string result;
        for(const auto& name : console_.Complete({})) result += name + '\n';
        return result;
    });
    console_.Register("echo", [](auto argument) { return std::string(argument); });
    console_.Register("log.clear", [this](auto) { logs_->Clear(); return std::string{}; });
    console_.Register("editor.undo", [this](auto) { return ConsoleService::Output{commands_.Execute("edit.undo") ? "" : "console.unavailable", true}; });
    console_.Register("editor.redo", [this](auto) { return ConsoleService::Output{commands_.Execute("edit.redo") ? "" : "console.unavailable", true}; });
    layout_.Initialize(settings_.UserDirectory());
    initialized_ = true;
    NEXUS_LOG_INFO("Editor", "Editor workspace initialized.");
}
void EditorWorkspace::Report(const Result<void>& result) {
    if(!result) NEXUS_LOG_ERROR("Editor", result.error().GetMessageText());
}
void EditorWorkspace::SynchronizeDocument() {
    if(generation_ == session_.Generation()) return;
    // World切替ではComponent参照を持ち越さず、ID履歴と選択を再同期する。
    properties_.Reset(session_.GetLevel()); generation_ = session_.Generation(); revision_ = properties_.Revision();
}
void EditorWorkspace::OpenLevel(const std::filesystem::path& path) {
    properties_.Commit();
    if(session_.IsDirty() || properties_.HasPendingChanges() || properties_.Revision() != revision_) {
        NEXUS_LOG_WARNING("Editor", text_.Text("file.saveFirst")); return;
    }
    auto result = session_.Open(path); Report(result);
    if(result) {
        SynchronizeDocument();
        RememberLevel(path);
    }
}
void EditorWorkspace::RememberLevel(const std::filesystem::path& path) {
    auto value = AssetCatalog::Utf8(path);
    std::erase(recent_, value); recent_.insert(recent_.begin(), value);
    if(recent_.size() > 10) recent_.resize(10);
    settings_.user.recentLevels = recent_;
    if(!settings_.SaveUser()) NEXUS_LOG_WARNING("Editor", "Failed to save recent levels.");
}
void EditorWorkspace::RegisterCommands() {
    const auto clean = [this] { return !session_.IsDirty() && !properties_.HasPendingChanges() && properties_.Revision() == revision_; };
    commands_.Register("file.new", {[this] { auto result = session_.Create(); Report(result); SynchronizeDocument(); }, clean});
    commands_.Register("file.open", {[this] { fileOperation_ = "open"; filePath_.fill(0); }, clean});
    commands_.Register("file.save", {[this] {
        properties_.Commit(); auto result = session_.GetLevelDocument()->Save(); Report(result);
        if(result) { session_.MarkSaved(); revision_ = properties_.Revision(); }
    }, [this] { return session_.GetLevelDocument() && session_.GetLevelDocument()->GetFilePath().has_value(); }});
    commands_.Register("file.saveAs", {[this] { fileOperation_ = "saveAs"; filePath_.fill(0); }, [this] { return session_.GetLevel() != nullptr; }});
    commands_.Register("file.exit", {[] { PostQuitMessage(0); }, clean});
    commands_.Register("edit.undo", {[this] { if(properties_.Undo()) session_.MarkDirty(); }, [this] { return properties_.CanUndo(); }});
    commands_.Register("edit.redo", {[this] { if(properties_.Redo()) session_.MarkDirty(); }, [this] { return properties_.CanRedo(); }});
    commands_.Register("edit.delete", {[this] {
        auto* level = session_.GetLevel(); if(!level) return;
        std::vector<PersistentId> selected(session_.Selection().All().begin(), session_.Selection().All().end());
        // 破棄後にUndoの古いIDを再適用しない。Entity削除Undoは未対応。
        for(auto id : selected) if(level->DestroyEntity(level->FindEntity(id))) session_.MarkDirty();
        session_.Selection().Clear(); properties_.Reset(level);
    }, [this] { return session_.GetLevel() && session_.GetSelectedEntity().IsAlive(); }});
    commands_.Register("edit.preferences", {[this] { panels_.Open("Preferences"); }, {}});
    commands_.Register("focus.details", {[this] { panels_.Open("Details"); }, {}});
    commands_.Register("window.reset", {[this] { layout_.Reset(); }, {}});
    commands_.Register("help.about", {[this] { showAbout_ = true; }, {}});
    // Cut/Copy/Paste/Duplicateは未登録のためMenuで無効になる。
}
void EditorWorkspace::ApplyUserSettings() {
    text_.SetLanguage(settings_.user.language);
    // 初期化とPreferencesが同じStyleを適用し、標準テーマでユーザー配色を上書きしない。
    UI::ApplyEditorStyle({settings_.user.theme == "Light", settings_.user.fontSize, settings_.user.uiScale});
    if(!settings_.SaveUser()) NEXUS_LOG_WARNING("Editor", "Failed to save user settings.");
    settingsChanged_ = false;
}
void EditorWorkspace::DrawFileDialog() {
    if(!fileOperation_.empty()) ImGui::OpenPopup("###LevelPath");
    auto title = text_.Label("file.path", "LevelPath");
    if(ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText(text_.Label("file.path", "path").c_str(), filePath_.data(), filePath_.size());
        if(ImGui::Button(text_.Label("apply", "apply").c_str()) && filePath_[0]) {
            auto path = std::filesystem::path{ConvertString(std::string{filePath_.data()})};
            if(fileOperation_ == "open") OpenLevel(path);
            else if(auto* document = session_.GetLevelDocument()) {
                properties_.Commit(); auto result = document->SaveAs(path); Report(result);
                if(result) { session_.MarkSaved(); revision_ = properties_.Revision(); RememberLevel(path); }
            }
            fileOperation_.clear(); ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if(ImGui::Button(text_.Label("cancel", "cancel").c_str())) { fileOperation_.clear(); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}
void EditorWorkspace::PrepareFrame() {
    if(initialized_ && settingsChanged_) ApplyUserSettings();
}
void EditorWorkspace::Draw() {
    if(!initialized_) return;
    SynchronizeDocument();
    if(properties_.Revision() != revision_) { revision_ = properties_.Revision(); session_.MarkDirty(); }
    layout_.Draw();
    menu_.Draw(text_, commands_, panels_, recent_, [this](const auto& path) { OpenLevel(std::filesystem::path{ConvertString(path)}); });
    // Shortcutsはtext編集中の入力を奪わない。
    if(ImGui::GetIO().KeyCtrl && !ImGui::GetIO().WantTextInput) {
        if(ImGui::IsKeyPressed(ImGuiKey_S)) commands_.Execute("file.save");
        if(ImGui::IsKeyPressed(ImGuiKey_Z)) commands_.Execute("edit.undo");
        if(ImGui::IsKeyPressed(ImGuiKey_Y)) commands_.Execute("edit.redo");
    }
    const EditorContext context{session_, session_.Selection(), properties_, types_, drawers_, text_, commands_};
    panels_.Draw(context);
    if(previousSelection_ != session_.GetSelection()) {
        properties_.Commit(); previousSelection_ = session_.GetSelection();
    }
    if(!ImGui::IsAnyItemActive()) properties_.Commit();
    if(properties_.HasPendingChanges()) session_.MarkDirty();
    if(properties_.Revision() != revision_) { revision_ = properties_.Revision(); session_.MarkDirty(); }
    DrawFileDialog();
    if(showAbout_) { ImGui::OpenPopup("###About"); showAbout_ = false; }
    if(ImGui::BeginPopupModal(text_.Label("help.about", "About").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(text_.Text("about.body").c_str());
        if(ImGui::Button(text_.Label("close", "close").c_str())) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
void EditorWorkspace::Shutdown() {
    if(!initialized_) return;
    properties_.Commit();
    for(const auto& entry : panels_.Entries()) settings_.user.panels[std::string(entry.panel->Id())] = entry.isOpen;
    if(!settings_.SaveUser()) NEXUS_LOG_WARNING("Editor", "Failed to save user settings.");
    layout_.Save(settings_.user.autoSaveLayout);
    viewport_.Detach();
    properties_.Reset(nullptr);
    if(logSink_) { Logger::Get().RemoveSink(logSink_); logSink_ = nullptr; }
    initialized_ = false;
}
}
