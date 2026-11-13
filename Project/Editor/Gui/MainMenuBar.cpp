#include "MainMenuBar.h"
#include "EditorTitleBarLayout.h"
#include <algorithm>
#include <filesystem>
#include "ThirdParty/imgui/imgui.h"

namespace NexusEngine {
void MainMenuBar::Draw(const LocalizationService& text, const EditorCommandRegistry& commands, PanelRegistry& panels,
    const std::vector<std::string>& recent, const std::function<void(const std::string&)>& openRecent,
    const std::function<std::vector<std::string>()>& listLevels, const std::string& activeLevel) {
    auto* title = EditorTitleBar::Current();
    const float scale = title ? EditorTitleBar::Scale(title) : 1;
    float menuStart = 0, menuRight = 0;
    if(title) {
        ImGui::Begin(EditorTitleBar::WindowName);
        title->DC.MenuBarOffset = {52*scale,std::max(0.f,(title->Size.y-ImGui::GetFrameHeight())*.5f)};
        if(!ImGui::BeginMenuBar()) { ImGui::End(); return; }
        menuStart = title->Pos.x+52*scale;
        // ロゴ、メニュー、エンジン名、Window操作の順で領域を予約する。
        menuRight = std::max(menuStart,title->Pos.x+title->Size.x-138*scale-ImGui::CalcTextSize("NexusEngine").x-32*scale);
        ImGui::PushClipRect({menuStart,title->Pos.y},{menuRight,title->Pos.y+title->Size.y},true);
    } else if(!ImGui::BeginMainMenuBar()) return;
    const auto drawMenus = [&] {
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
        ImGui::Separator(); item("edit.create", "scene.entity"); item("scene.model", "scene.model"); item("scene.camera", "scene.camera"); item("scene.light", "scene.light"); item("scene.sky", "scene.sky");
        ImGui::Separator(); item("edit.cut", "edit.cut"); item("edit.copy", "edit.copy"); item("edit.paste", "edit.paste");
        item("edit.duplicate", "edit.duplicate"); item("edit.delete", "edit.delete");
        ImGui::Separator(); item("edit.preferences", "edit.preferences"); ImGui::EndMenu();
    }
    if(ImGui::BeginMenu(text.Label("menu.play", "Play").c_str())) {
        if(ImGui::BeginMenu(text.Label("play.mode", "PlayMode").c_str())) {
            for(const auto* id : {"play.mode.editor","play.mode.window"})
                if(ImGui::MenuItem(text.Label(id,id).c_str(),nullptr,commands.IsChecked(id),commands.CanExecute(id))) commands.Execute(id);
            ImGui::EndMenu();
        }
        ImGui::Separator(); item("play.start","play.start");
        item(commands.IsChecked("play.pause") ? "play.resume" : "play.pause","play.pause"); item("play.stop","play.stop");
        ImGui::EndMenu();
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
    if(ImGui::BeginMenu(text.Label("menu.help", "Help").c_str())) {
        item("help.iconGuide", "help.iconGuide"); item("help.about", "help.about"); ImGui::EndMenu();
    }
    };
    if(title) {
        float required = 0;
        for(const auto* key : {"menu.file","menu.level","menu.edit","menu.play","menu.window","menu.tools","menu.help"}) {
            if(std::string_view(key) == "menu.level" && !listLevels) continue;
            required += ImGui::CalcTextSize(text.Text(key).c_str()).x+ImGui::GetStyle().ItemSpacing.x*2;
        }
        if(required <= menuRight-menuStart) drawMenus();
        else {
            // 狭い幅でも全メニューを使えるよう、同じ内容を一つのMenu配下へ構成する。
            if(ImGui::BeginMenu("...###AllMenus")) { drawMenus(); ImGui::EndMenu(); }
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",text.Text("menu.all").c_str());
        }
        const float end = std::clamp(ImGui::GetCursorScreenPos().x,menuStart,menuRight);
        EditorTitleBar::SetMenuBounds(title,{{menuStart,title->Pos.y},{end,title->Pos.y+title->Size.y}});
        ImGui::PopClipRect(); ImGui::EndMenuBar(); ImGui::End();
    } else { drawMenus(); ImGui::EndMainMenuBar(); }
}
}
