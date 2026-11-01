#include "Editor/Gui/MainMenuBar.h"
#include "ThirdParty/imgui/imgui.h"
#include "ThirdParty/imgui/imgui_internal.h"
#include <cassert>
#include <iostream>
#include <crtdbg.h>
#include <cstdlib>

void RunLevelMenuTests() {
    _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
    _set_abort_behavior(0,_WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    using namespace NexusEngine;
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.DisplaySize = {800,600}; io.DeltaTime = 1.f/60;
    unsigned char* pixels; int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    MainMenuBar menu; PanelRegistry panels; LocalizationService text;
    assert(text.Load("Project/Resources/Editor/Localization"));
    EditorCommandRegistry commands;
    bool clean = true;
    assert(commands.Register("file.open",{[]{},[&]{return clean;}}));
    const std::vector<std::string> levels{"DemoLevel.json","PBRTestLevel.json","SponzaLevel.json"};
    std::string opened;
    auto frame = [&] {
        ImGui::NewFrame();
        menu.Draw(text,commands,panels,{},[&](const auto& path){opened=path;},[&]{return levels;},"DemoLevel.json");
        ImGui::Render();
    };
    frame();
    auto* bar = ImGui::FindWindowByName("##MainMenuBar"); assert(bar);
    const ImGuiID levelMenuId = ImHashStr("###Level",0,bar->GetID("##MenuBar"));
    ImGui::ActivateItemByID(levelMenuId); frame(); frame();
    auto* popup = ImGui::FindWindowByName("###Menu_00"); assert(popup && popup->Active);
    ImGui::ActivateItemByID(popup->GetID("SponzaLevel")); frame();
    assert(opened=="SponzaLevel.json");
    opened.clear(); clean=false;
    ImGui::ActivateItemByID(levelMenuId); frame(); frame();
    popup = ImGui::FindWindowByName("###Menu_00"); assert(popup && popup->Active);
    ImGui::ActivateItemByID(popup->GetID("DemoLevel")); frame();
    assert(opened.empty());
    ImGui::DestroyContext();
    std::cout << "Level menu selection and unsaved-edit protection passed\n";
}

void RunExecutionModeMenuTests() {
    using namespace NexusEngine;
    ImGui::CreateContext();
    auto& io = ImGui::GetIO(); io.IniFilename = nullptr; io.DisplaySize = {900,600}; io.DeltaTime = 1.f/60;
    unsigned char* pixels; int width,height; io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    MainMenuBar menu; PanelRegistry panels; LocalizationService text;
    assert(text.Load("Project/Resources/Editor/Localization"));
    EditorCommandRegistry commands;
    bool separate = false, startedInSeparateWindow = false; int starts = 0;
    EditorCommandRegistry::Command inEditor{[&] { separate = false; },[] { return true; }};
    inEditor.isChecked = [&] { return !separate; };
    EditorCommandRegistry::Command inWindow{[&] { separate = true; },[] { return true; }};
    inWindow.isChecked = [&] { return separate; };
    assert(commands.Register("play.mode.editor",std::move(inEditor)));
    assert(commands.Register("play.mode.window",std::move(inWindow)));
    assert(commands.Register("play.start",{[&] { startedInSeparateWindow = separate; ++starts; },[] { return true; }}));
    const auto frame = [&] { ImGui::NewFrame(); menu.Draw(text,commands,panels,{},[](const auto&) {}); ImGui::Render(); };
    frame(); auto* bar = ImGui::FindWindowByName("##MainMenuBar"); assert(bar);
    const auto runMenu = ImHashStr("###Play",0,bar->GetID("##MenuBar"));
    ImGui::ActivateItemByID(runMenu); frame(); frame();
    auto* popup = ImGui::FindWindowByName("###Menu_00"); assert(popup && popup->Active);
    ImGui::ActivateItemByID(popup->GetID("###PlayMode")); frame(); frame();
    auto* modes = ImGui::FindWindowByName("###Menu_01"); assert(modes && modes->Active);
    ImGui::ActivateItemByID(modes->GetID("###play.mode.window")); frame();
    assert(separate && commands.IsChecked("play.mode.window") && !commands.IsChecked("play.mode.editor"));
    ImGui::ActivateItemByID(runMenu); frame(); frame();
    popup = ImGui::FindWindowByName("###Menu_00"); assert(popup && popup->Active);
    ImGui::ActivateItemByID(popup->GetID("###play.start")); frame();
    assert(starts == 1 && startedInSeparateWindow);
    ImGui::DestroyContext();
    std::cout << "PASS: Run menu opens its mode submenu, selects Separate Window and invokes the shared Run command.\n";
}
