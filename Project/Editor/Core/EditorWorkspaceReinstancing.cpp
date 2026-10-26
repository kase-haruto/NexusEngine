#include "EditorWorkspace.h"
#include "ThirdParty/imgui/imgui.h"
#include "ThirdParty/imgui/imgui_internal.h"

namespace NexusEngine {
ReflectionValue EditorWorkspace::CaptureForReinstancing() {
    auto viewport = viewportTools_.CaptureForReinstancing();
    properties_.Commit();
    ReflectionValue::Object panels;
    for(const auto& entry : panels_.Entries()) panels.emplace(entry.id, CaptureReflection(entry.isOpen));
    size_t iniSize = 0;
    const char* ini = ImGui::GetCurrentContext() ? ImGui::SaveIniSettingsToMemory(&iniSize) : "";

    // 描画コールバックは持ち越さず、セクションIDに対応するオフセット値だけを保存する。
    ReflectionValue::Object toolbarOffsets;
    for(const auto& [id, offset] : toolbar_.GetOffsets()) {
        toolbarOffsets.emplace(id, CaptureReflection(offset));
    }
    return ReflectionValue(ReflectionValue::Object{
        {"toolbarOffsets", ReflectionValue(std::move(toolbarOffsets))},
        {"dirty", CaptureReflection(documents_.IsDirty())}, {"panels", ReflectionValue(std::move(panels))},
        {"viewport", std::move(viewport)}, {"dpi", CaptureReflection(dpiScale_)},
        {"layout", CaptureReflection(std::string(ini, iniSize))},
        {"language", CaptureReflection(settings_.user.language)}, {"theme", CaptureReflection(settings_.user.theme)},
        {"fontSize", CaptureReflection(settings_.user.fontSize)}, {"uiScale", CaptureReflection(settings_.user.uiScale)},
        {"autoSaveLayout", CaptureReflection(settings_.user.autoSaveLayout)},
        {"workspace", CaptureReflection(settings_.user.workspace)}, {"recentLevels", CaptureReflection(recent_)},
        {"giMode", CaptureReflection(settings_.editor.giMode)}, {"grid", CaptureReflection(settings_.editor.grid)},
        {"gizmo", CaptureReflection(settings_.editor.gizmo)}, {"viewportOpen", CaptureReflection(settings_.editor.viewport)}});
}
void EditorWorkspace::ReleaseForReinstancing() noexcept {
    // ここでは設定ファイルを保存せず、破棄前に非所有参照と操作履歴を切断する。
    // Toolbarのコールバックは旧コードの関数を参照するため、再登録できるよう破棄する。
    toolbar_.Clear();
    panels_.Clear();
    viewport_.Detach();
    properties_.Reset(nullptr);
    if(ImGui::GetCurrentContext()) ImGui::ClearActiveID();
}
void EditorWorkspace::RestoreAfterReinstancing(const ReflectionValue& snapshot) {
    // 古いコードで取得したスナップショットには配置情報がないため、存在する場合だけ復元する。
    const auto& fields = std::get<ReflectionValue::Object>(snapshot.data);
    if(auto found = fields.find("toolbarOffsets"); found != fields.end()) {
        ToolbarOffsets offsets;
        const auto& values = std::get<ReflectionValue::Object>(found->second.data);
        for(const auto& [id, value] : values) {
            ToolbarOffset offset{};
            RestoreReflection(value, offset);
            offsets.emplace(id, offset);
        }
        toolbar_.SetOffsets(offsets);
        settings_.user.toolbarOffsets = toolbar_.GetOffsets();
        toolbarLayoutRevision_ = toolbar_.LayoutRevision();
    }
    bool dirty = false; RestoreReflection(snapshot.At("dirty"), dirty);
    if(dirty) session_.MarkDirty();
    const auto& panels = std::get<ReflectionValue::Object>(snapshot.At("panels").data);
    for(auto& entry : panels_.Entries()) {
        if(auto found = panels.find(entry.id); found != panels.end()) RestoreReflection(found->second, entry.isOpen);
    }
    RestoreReflection(snapshot.At("dpi"), dpiScale_);
    RestoreReflection(snapshot.At("language"), settings_.user.language);
    RestoreReflection(snapshot.At("theme"), settings_.user.theme);
    RestoreReflection(snapshot.At("fontSize"), settings_.user.fontSize);
    RestoreReflection(snapshot.At("uiScale"), settings_.user.uiScale);
    RestoreReflection(snapshot.At("autoSaveLayout"), settings_.user.autoSaveLayout);
    RestoreReflection(snapshot.At("workspace"), settings_.user.workspace);
    RestoreReflection(snapshot.At("recentLevels"), recent_); settings_.user.recentLevels = recent_;
    RestoreReflection(snapshot.At("giMode"), settings_.editor.giMode);
    RestoreReflection(snapshot.At("grid"), settings_.editor.grid);
    RestoreReflection(snapshot.At("gizmo"), settings_.editor.gizmo);
    RestoreReflection(snapshot.At("viewportOpen"), settings_.editor.viewport);
    text_.SetLanguage(settings_.user.language); settingsChanged_ = true;
    viewportTools_.RestoreAfterReinstancing(snapshot.At("viewport"));
    if(ImGui::GetCurrentContext()) {
        const auto& ini = std::get<std::string>(snapshot.At("layout").data);
        ImGui::LoadIniSettingsFromMemory(ini.data(), ini.size());
    }
}
}
