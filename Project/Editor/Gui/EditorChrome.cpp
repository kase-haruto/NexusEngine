#include "EditorChrome.h"
#include "ThirdParty/imgui/imgui.h"
#include "ThirdParty/imgui/imgui_internal.h"

namespace NexusEngine {
void DrawEditorToolbar(const LocalizationService& text, const EditorCommandRegistry& commands,
    std::string_view document, bool dirty, bool playing, bool paused) {
    const auto flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
    if(ImGui::BeginViewportSideBar("###EditorToolbar", ImGui::GetMainViewport(), ImGuiDir_Up,
        ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().WindowPadding.y * 2, flags)) {
        const auto button = [&](const char* key, const char* id) {
            ImGui::BeginDisabled(!commands.CanExecute(id));
            if(ImGui::Button(text.Label(key, id).c_str())) commands.Execute(id);
            ImGui::EndDisabled();
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s %s", text.Text(key).c_str(), commands.Shortcut(id));
            ImGui::SameLine();
        };
        button("file.save", "file.save"); button("edit.undo", "edit.undo"); button("edit.redo", "edit.redo");
        button("edit.create", "scene.entity"); button("play.start", "play.start");
        button(paused ? "play.resume" : "play.pause", "play.pause"); button("play.stop", "play.stop");
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical); ImGui::SameLine();
        ImGui::TextUnformatted(document.data(), document.data() + document.size());
        if(dirty) { ImGui::SameLine(); ImGui::TextUnformatted("*"); }
        ImGui::SameLine(); ImGui::TextDisabled("%s", text.Text(playing ? (paused ? "runtime.paused" : "runtime.running") : "editor.editing").c_str());
    }
    ImGui::End();
}
void DrawEditorNotifications(NotificationService& notifications) {
    const auto& items = notifications.Items();
    if(items.empty()) return;
    const auto* viewport = ImGui::GetMainViewport();
    const auto padding = ImGui::GetStyle().WindowPadding;
    ImGui::SetNextWindowPos({viewport->WorkPos.x + viewport->WorkSize.x - padding.x,
        viewport->WorkPos.y + viewport->WorkSize.y - padding.y}, ImGuiCond_Always, {1, 1});
    ImGui::SetNextWindowBgAlpha(0.95f);
    if(ImGui::Begin("###Notifications", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing)) {
        for(const auto& item : items) {
            ImGui::TextUnformatted(item.error ? "!" : "+"); ImGui::SameLine();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28);
            ImGui::TextUnformatted(item.text.c_str()); ImGui::PopTextWrapPos();
        }
    }
    ImGui::End();
}
}
