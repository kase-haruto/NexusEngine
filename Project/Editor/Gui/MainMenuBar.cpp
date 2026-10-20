#include "MainMenuBar.h"
#include "ThirdParty/imgui/imgui.h"

namespace NexusEngine {
void MainMenuBar::Draw(const LocalizationService& text, const EditorCommandRegistry& commands, PanelRegistry& panels,
    const std::vector<std::string>& recent, const std::function<void(const std::string&)>& openRecent) {
    if(!ImGui::BeginMainMenuBar()) return;
    const auto item = [&](const char* key, const char* id, const char* shortcut = nullptr) {
        if(ImGui::MenuItem(text.Label(key, id).c_str(), shortcut, false, commands.CanExecute(id))) commands.Execute(id);
    };
    if(ImGui::BeginMenu(text.Label("menu.file", "File").c_str())) {
        item("file.new", "file.new"); item("file.open", "file.open"); item("file.save", "file.save", "Ctrl+S"); item("file.saveAs", "file.saveAs");
        if(ImGui::BeginMenu(text.Label("file.recent", "Recent").c_str(), !recent.empty() && commands.CanExecute("file.open"))) {
            for(const auto& path : recent) if(ImGui::MenuItem(path.c_str())) openRecent(path);
            ImGui::EndMenu();
        }
        ImGui::Separator(); item("file.exit", "file.exit"); ImGui::EndMenu();
    }
    if(ImGui::BeginMenu(text.Label("menu.edit", "Edit").c_str())) {
        item("edit.undo", "edit.undo", "Ctrl+Z"); item("edit.redo", "edit.redo", "Ctrl+Y");
        ImGui::Separator(); item("edit.cut", "edit.cut"); item("edit.copy", "edit.copy"); item("edit.paste", "edit.paste");
        item("edit.duplicate", "edit.duplicate"); item("edit.delete", "edit.delete");
        ImGui::Separator(); item("edit.preferences", "edit.preferences"); ImGui::EndMenu();
    }
    if(ImGui::BeginMenu(text.Label("menu.window", "Window").c_str())) {
        for(auto& entry : panels.Entries()) ImGui::MenuItem(text.Label(entry.panel->TitleKey(), entry.panel->Id()).c_str(), nullptr, &entry.isOpen);
        ImGui::Separator(); item("window.reset", "window.reset"); ImGui::EndMenu();
    }
    if(ImGui::BeginMenu(text.Label("menu.tools", "Tools").c_str())) {
        for(const auto& tool : tools_) item(tool.labelKey.c_str(), tool.command.c_str());
        if(tools_.empty()) ImGui::TextDisabled("%s", text.Text("tools.empty").c_str());
        ImGui::EndMenu();
    }
    if(ImGui::BeginMenu(text.Label("menu.help", "Help").c_str())) { item("help.about", "help.about"); ImGui::EndMenu(); }
    ImGui::EndMainMenuBar();
}
}
