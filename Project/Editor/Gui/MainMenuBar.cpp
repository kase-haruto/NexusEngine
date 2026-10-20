#include "MainMenuBar.h"
#include <filesystem>
#include "ThirdParty/imgui/imgui.h"

namespace NexusEngine {
void MainMenuBar::Draw(const LocalizationService& text, const EditorCommandRegistry& commands, PanelRegistry& panels,
    const std::vector<std::string>& recent, const std::function<void(const std::string&)>& openRecent,
    const std::function<std::vector<std::string>()>& listLevels, const std::string& activeLevel) {
    if(!ImGui::BeginMainMenuBar()) return;
    const auto item = [&](const char* key, const char* id) {
        if(ImGui::MenuItem(text.Label(key, id).c_str(), commands.Shortcut(id), false, commands.CanExecute(id))) commands.Execute(id);
    };
    if(ImGui::BeginMenu(text.Label("menu.file", "File").c_str())) {
        item("file.new", "file.new"); item("file.open", "file.open"); item("file.save", "file.save"); item("file.saveAs", "file.saveAs");
        if(ImGui::BeginMenu(text.Label("file.recent", "Recent").c_str(), !recent.empty() && commands.CanExecute("file.open"))) {
            for(const auto& path : recent) if(ImGui::MenuItem(path.c_str())) openRecent(path);
            ImGui::EndMenu();
        }
        ImGui::Separator(); item("file.exit", "file.exit"); ImGui::EndMenu();
    }
    if(listLevels && ImGui::BeginMenu(text.Label("menu.level", "Level").c_str())) {
        const auto levels = listLevels();
        if(!activeLevel.empty()) ImGui::TextDisabled("%s", activeLevel.c_str());
        ImGui::BeginDisabled(!commands.CanExecute("file.open"));
        for(const auto& path : levels) {
            auto label = std::filesystem::path{std::u8string(reinterpret_cast<const char8_t*>(path.data()), path.size())}.stem().u8string();
            if(ImGui::MenuItem(reinterpret_cast<const char*>(label.c_str()), nullptr, path == activeLevel)) openRecent(path);
        }
        ImGui::EndDisabled();
        if(!commands.CanExecute("file.open")) ImGui::TextWrapped("%s", text.Text("file.saveFirst").c_str());
        ImGui::EndMenu();
    }
    if(ImGui::BeginMenu(text.Label("menu.edit", "Edit").c_str())) {
        item("edit.undo", "edit.undo"); item("edit.redo", "edit.redo");
        ImGui::Separator(); item("edit.create", "scene.entity"); item("scene.model", "scene.model"); item("scene.camera", "scene.camera"); item("scene.light", "scene.light");
        ImGui::Separator(); item("edit.cut", "edit.cut"); item("edit.copy", "edit.copy"); item("edit.paste", "edit.paste");
        item("edit.duplicate", "edit.duplicate"); item("edit.delete", "edit.delete");
        ImGui::Separator(); item("edit.preferences", "edit.preferences"); ImGui::EndMenu();
    }
    if(ImGui::BeginMenu(text.Label("menu.window", "Window").c_str())) {
        for(auto& entry : panels.Entries()) ImGui::MenuItem(text.Label(entry.TitleKey(), entry.Id()).c_str(), nullptr, &entry.isOpen);
        if(ImGui::BeginMenu(text.Label("workspace.layouts", "Workspaces").c_str())) {
            item("workspace.General", "workspace.General"); item("workspace.Lighting", "workspace.Lighting"); item("workspace.Animation", "workspace.Animation");
            ImGui::EndMenu();
        }
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
