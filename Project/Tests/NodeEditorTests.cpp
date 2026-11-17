#include "Editor/Gui/Nodes/NodeEditor.h"
#include "ThirdParty/imnodes/imnodes.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace NexusEngine::Tests {
namespace {
void ExpectNodes(bool value, const char* message) {
    if(!value) {
        throw std::runtime_error(message);
    }
}
void DrawNode(UI::NodeEditor& editor) {
    editor.BeginNode(1);
    editor.BeginTitle();
    ImGui::TextUnformatted("Shared node");
    editor.EndTitle();
    editor.BeginInput(3);
    ImGui::TextUnformatted("Input");
    editor.EndInput();
    editor.BeginOutput(4);
    ImGui::TextUnformatted("Output");
    editor.EndOutput();
    editor.EndNode();
}
}
void RunNodeEditorTests() {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.DisplaySize = {1400,900};
    io.DeltaTime = 1.0f/60;
    io.IniFilename = nullptr;
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    io.Fonts->SetTexID(1);
    {
        UI::NodeEditor first, second;
        for(int frame = 0; frame < 3; ++frame) {
            ImGui::NewFrame();
            ImGui::SetNextWindowSize({1200,800});
            ImGui::Begin("Node wrapper tests");
            const auto padding = ImGui::GetStyle().WindowPadding;
            auto* previous = ImNodes::GetCurrentContext();
            {
                UI::NodeEditorSession session(first);
                {
                    ImGui::BeginChild("first canvas",{550,300});
                    UI::NodeEditorCanvas canvas(first,.5f);
                    first.SetNodePosition(1,{20,20});
                    DrawNode(first);
                    canvas.Close();
                    ImGui::EndChild();
                }
                ExpectNodes(ImGui::GetStyle().WindowPadding.x == padding.x,"Node zoom leaked into ImGui style.");
                first.SelectNode(1);
                first.SetPanning({30,20});
                ExpectNodes(first.ZoomAt(1,2,{100,100}) == 2,"Shared zoom failed.");
                const auto pan = first.Panning();
                ExpectNodes(std::abs(pan[0]+40)<.001f && std::abs(pan[1]+60)<.001f,"Cursor anchored zoom moved its logical point.");
                {
                    UI::NodeEditorSession nested(second);
                    ImGui::BeginChild("second canvas",{550,300});
                    UI::NodeEditorCanvas canvas(second,1.5f);
                    second.SetNodePosition(1,{70,20});
                    DrawNode(second);
                    canvas.Close();
                    ImGui::EndChild();
                    ExpectNodes(first.NativeContext() != second.NativeContext(),"Editors shared their native context.");
                    ExpectNodes(second.SelectedNodes().empty(),"Selection leaked into another node editor.");
                }
                ExpectNodes(ImNodes::GetCurrentContext() == first.NativeContext(),"Nested editor did not restore its previous context.");
                ExpectNodes(first.NodeSelected(1),"Another editor changed the first selection.");
                ExpectNodes(first.Panning() == pan,"Another editor changed the first view.");
            }
            ExpectNodes(ImNodes::GetCurrentContext() == previous,"Session leaked the current context.");
            ExpectNodes(ImGui::GetStyle().WindowPadding.x == padding.x,"Canvas did not restore style.");
            ImGui::End();
            ImGui::Render();
        }
    }
    ImGui::DestroyContext();
    std::cout << "PASS: shared ImNodes wrapper context/selection/view isolation, nested restoration and cursor anchored zoom\n";
}
}
