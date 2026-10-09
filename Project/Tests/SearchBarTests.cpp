#include "Editor/Gui/Widgets/SearchBar.h"
#include "ThirdParty/imgui/imgui.h"
#include "ThirdParty/imgui/imgui_internal.h"
#include <cassert>
#include <iostream>
#include <string>

// GPU/Windowを使わず入力イベントを送り、外部状態とImGui登録の契約を検証する。
int main() {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(800, 600);
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    std::string first, second = "independent";
    ImGuiID firstId = 0, secondId = 0;
    bool changed = false;
    bool focus = false;
    auto frame = [&](const char* hint = "Search...") {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(700, 500));
        ImGui::Begin("Test", nullptr, ImGuiWindowFlags_NoSavedSettings);
        const auto padding = ImGui::GetStyle().FramePadding;
        const int colors = GImGui->ColorStack.Size;
        const int vars = GImGui->StyleVarStack.Size;
        if(focus) ImGui::SetKeyboardFocusHere();
        changed = NexusEngine::UI::SearchBar("first", first, hint, 300);
        firstId = ImGui::GetItemID();
        assert(ImGui::GetItemRectSize().x == 300);
        assert(ImGui::GetItemRectSize().y == 32);
        ImGui::SameLine();
        assert(!NexusEngine::UI::SearchBar("second", second, "Other", 180));
        secondId = ImGui::GetItemID();
        assert(firstId != secondId && second == "independent");
        assert(GImGui->ColorStack.Size == colors && GImGui->StyleVarStack.Size == vars);
        assert(ImGui::GetStyle().FramePadding.x == padding.x);
        ImGui::BeginGroup();
        ImGui::BeginChild("Child", ImVec2(500, 100));
        if(ImGui::BeginTable("Table", 2)) {
            ImGui::TableNextColumn();
            std::string other;
            assert(!NexusEngine::UI::SearchBar("cell", other));
            ImGui::TableNextColumn();
            ImGui::Button("Following");
            ImGui::EndTable();
        }
        ImGui::EndChild();
        ImGui::EndGroup();
        ImGui::End();
        ImGui::Render();
    };
    frame();
    assert(!changed);
    focus = true;
    frame();
    focus = false;
    frame();
    assert(ImGui::GetInputTextState(firstId));
    io.AddInputCharactersUTF8("abc");
    frame();
    assert(changed && first == "abc");
    frame("New hint");
    assert(!changed && first == "abc");
    first = "external";
    frame();
    assert(!changed && first == "external");
    assert(std::string(ImGui::GetInputTextState(firstId)->GetText()) == first);
    first.clear();
    frame();
    assert(!changed && first.empty());
    const std::string large(8192, 'x');
    io.AddInputCharactersUTF8(large.c_str());
    frame();
    assert(changed && first == large);
    ImGui::GetInputTextState(firstId)->SelectAll();
    io.AddKeyEvent(ImGuiKey_Backspace, true);
    frame();
    assert(changed && first.empty());
    io.AddKeyEvent(ImGuiKey_Backspace, false);
    frame();
    assert(!changed);
    io.AddInputCharactersUTF8("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E");
    frame();
    assert(changed && first.size() == 9);
    ImGui::DestroyContext();
    std::cout << "SearchBar headless tests passed\n";
}
