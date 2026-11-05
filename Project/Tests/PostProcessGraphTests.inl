#include "Graphics/PostProcess/PostProcessGraph.h"
#include "Editor/Services/PostProcessGraphDocument.h"
#include "Editor/Gui/Panels/PostProcessGraphPanel.h"
#include "Graphics/PostProcess/PostProcessGraphAsset.h"
#include "ThirdParty/imnodes/imnodes.h"

namespace NexusEngine {
struct PostProcessGraphPanelTestAccess {
    static float Zoom(const PostProcessGraphPanel& panel) { return panel.zoom_; }
    static ImVec2 CanvasOrigin(const PostProcessGraphPanel& panel) { return {panel.canvasOrigin_[0],panel.canvasOrigin_[1]}; }
    static ImVec2 CanvasSize(const PostProcessGraphPanel& panel) { return {panel.canvasSize_[0],panel.canvasSize_[1]}; }
    static ImVec2 Pan(PostProcessGraphPanel& panel) {
        auto* previous=ImNodes::GetCurrentContext(); ImNodes::SetCurrentContext(panel.nodes_);
        auto result=ImNodes::EditorContextGetPanning(); ImNodes::SetCurrentContext(previous); return result;
    }
    static ImVec2 NodePosition(PostProcessGraphPanel& panel,int id) {
        auto* previous=ImNodes::GetCurrentContext(); ImNodes::SetCurrentContext(panel.nodes_);
        auto result=ImNodes::GetNodeScreenSpacePos(id); ImNodes::SetCurrentContext(previous); return result;
    }
    static std::map<int,ImVec2> Dimensions(PostProcessGraphPanel& panel) {
        auto* previous=ImNodes::GetCurrentContext(); ImNodes::SetCurrentContext(panel.nodes_);
        std::map<int,ImVec2> result;
        for(const auto& node:panel.document_.Graph().nodes) result[node.id]=ImNodes::GetNodeDimensions(node.id);
        ImNodes::SetCurrentContext(previous); return result;
    }
};
}

void PostProcessGraphTests() {
    auto graph=PostProcessGraph::Default();
    auto plan=graph.Compile(); assert(plan && plan->settings.bloomEnabled && !plan->settings.antiAliasingEnabled);
    assert(plan->effects.size()==1 && plan->effects[0].shaderPath==L"PostProcess/Fxaa.hlsl");
    const auto links=graph.links;
    assert(!graph.Connect(1,5)); // HDR cannot feed display output.
    assert(graph.links.size()==links.size() && graph.Compile());
    PostProcessNode vignette; vignette.id=6; vignette.kind=PostProcessNodeKind::Vignette; vignette.effect.parameters[0]=0.7f;
    graph.nodes.push_back(vignette);
    assert(graph.Connect(3,6) && graph.Connect(6,4));
    plan=graph.Compile(); assert(plan && plan->effects.size()==2 && plan->order==std::vector<int>({1,2,3,6,4,5}));
    assert(!graph.Connect(4,6)); // Cycle rejection also preserves the previous input.
    assert(graph.Compile());
    assert(graph.Connect(3,4) && graph.Connect(4,6) && graph.Connect(6,5));
    plan=graph.Compile(); assert(plan && plan->effects[0].shaderPath==L"PostProcess/Fxaa.hlsl" && plan->effects[1].shaderPath==L"PostProcess/Vignette.hlsl");
    graph.links.pop_back(); assert(!graph.Compile());
    assert(graph.Connect(6,5));
    graph.nodes[1].enabled=false; plan=graph.Compile(); assert(plan && !plan->settings.bloomEnabled);
    graph.nodes.push_back(graph.nodes[0]); assert(!graph.Compile()); graph.nodes.pop_back();

    const auto root=std::filesystem::absolute("Generated/PostProcessGraphTests");
    PostProcessNode draft; draft.id=7; draft.kind=PostProcessNodeKind::Shader; draft.effect.shaderPath=L"Assets/Shaders/テスト.hlsl";
    graph.nodes.push_back(draft);
    PostProcessGraphDocument document; document.Initialize(root); document.Graph()=graph;
    document.Graph().nodes[2].position={123,456}; document.Changed();
    auto pending=document.TakePending(); assert(pending && !document.TakePending());
    assert(pending->nodes.back().effect.shaderPath==root/draft.effect.shaderPath);
    auto serialized=document.Serialize(); assert(document.Save() && !document.IsDirty());
    auto runtimeGraph=LoadPostProcessGraph(root/"Config/RenderPipeline.json"); assert(runtimeGraph && runtimeGraph->Compile());
    document.Reset(); assert(document.Load());
    assert(document.Serialize()==serialized);
    assert(!document.Deserialize("{ broken JSON")); assert(document.Serialize()==serialized);
    document.autoApply=false; (void)document.TakePending(); document.Changed(); assert(!document.TakePending());
    document.RequestApply(); assert(document.TakePending());

    Fixture f; PropertyDrawerRegistry drawers; drawers.RegisterDefaults();
    LocalizationService text; assert(text.Load("Project/Resources/Editor/Localization"));
    EditorCommandRegistry commands;
    EditorContext context{f.session,f.session.Selection(),f.properties,f.types,drawers,text,commands,f.edits};
    ImGui::CreateContext(); auto& io=ImGui::GetIO(); io.DisplaySize={1500,900}; io.DeltaTime=1.0f/60; io.IniFilename=nullptr;
    unsigned char* pixels; int width,height; io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height); io.Fonts->SetTexID(1);
    {
        PostProcessGraphPanel panel(document);
        std::map<int,ImVec2> baseline;
        for(int frame=0;frame<90;++frame) {
            if(frame==2) text.SetLanguage("ja");
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(frame<60?ImVec2{0,0}:ImVec2{60,40});
            ImGui::SetNextWindowSize(frame>=30 && frame<60?ImVec2{850,680}:ImVec2{1400,800});
            ImGui::Begin("Graph tests",nullptr,ImGuiWindowFlags_NoSavedSettings); panel.Draw(context); ImGui::End(); ImGui::Render();
            assert(ImGui::GetDrawData()->TotalVtxCount>500);
            auto dimensions=PostProcessGraphPanelTestAccess::Dimensions(panel);
            if(frame==4) baseline=dimensions;
            if(frame==89) for(const auto& [id,size]:dimensions) {
                assert(size.x<450 && size.y<450);
                assert(std::abs(size.x-baseline.at(id).x)<1 && std::abs(size.y-baseline.at(id).y)<1);
            }
        }
        using Access=PostProcessGraphPanelTestAccess;
        auto frame=[&](ImVec2 mouse,float wheel=0,bool right=false,bool left=false,bool middle=false) {
            io.AddMousePosEvent(mouse.x,mouse.y); io.AddMouseWheelEvent(0,wheel);
            io.AddMouseButtonEvent(0,left); io.AddMouseButtonEvent(1,right); io.AddMouseButtonEvent(2,middle);
            ImGui::NewFrame(); ImGui::SetNextWindowPos({200,40}); ImGui::SetNextWindowSize({1200,800});
            ImGui::Begin("Graph tests",nullptr,ImGuiWindowFlags_NoSavedSettings); panel.Draw(context); ImGui::End(); ImGui::Render();
        };
        frame({-100,-100}); frame({-100,-100});
        const auto viewOnlySnapshot=document.Serialize();
        const auto origin=Access::CanvasOrigin(panel), canvas=Access::CanvasSize(panel);
        const ImVec2 anchor{origin.x+70,origin.y+canvas.y-120};
        const auto worldAt=[&](ImVec2 mouse) {
            auto o=Access::CanvasOrigin(panel), pan=Access::Pan(panel); float z=Access::Zoom(panel);
            return ImVec2{(mouse.x-o.x-pan.x)/z,(mouse.y-o.y-pan.y)/z};
        };
        frame(anchor);
        const auto anchoredWorld=worldAt(anchor), initialSize=Access::Dimensions(panel).at(2);
        const float padding=ImGui::GetStyle().WindowPadding.x;
        frame(anchor,1); frame(anchor);
        assert(Access::Zoom(panel)>1);
        const auto afterZoom=worldAt(anchor), scaledSize=Access::Dimensions(panel).at(2);
        assert(std::abs(anchoredWorld.x-afterZoom.x)<.01f && std::abs(anchoredWorld.y-afterZoom.y)<.01f);
        assert(std::abs(scaledSize.x-initialSize.x*Access::Zoom(panel))<5);
        assert(ImGui::GetStyle().WindowPadding.x==padding && document.Serialize()==viewOnlySnapshot);
        const float zoom=Access::Zoom(panel);
        frame({215,55},5); frame({215,55}); assert(Access::Zoom(panel)==zoom); // Toolbar must not zoom the canvas.
        frame(anchor,1000); frame(anchor); assert(Access::Zoom(panel)==2);
        // Mouse motion followed by a wheel event can be trickled across frames.
        // Zoom is committed after the canvas draw, so measure the next rendered frame.
        frame(anchor);
        const auto enlarged=Access::Dimensions(panel);
        for(int stable=0;stable<12;++stable) {
            frame(anchor);
            for(const auto& [id,size]:Access::Dimensions(panel)) {
                assert(std::abs(size.x-enlarged.at(id).x)<1 && std::abs(size.y-enlarged.at(id).y)<1);
            }
        }
        frame(anchor,-1000); frame(anchor); assert(Access::Zoom(panel)==.25f);
        assert(document.Serialize()==viewOnlySnapshot);
        const auto panBefore=Access::Pan(panel);
        frame(anchor,0,false,false,true);
        frame({anchor.x+30,anchor.y+20},0,false,false,true);
        frame({anchor.x+30,anchor.y+20});
        const auto panAfter=Access::Pan(panel);
        assert(std::abs(panAfter.x-panBefore.x-30)<1 && std::abs(panAfter.y-panBefore.y-20)<1);
        assert(document.Serialize()==viewOnlySnapshot);

        const auto click=[&](ImVec2 mouse,bool right=false) {
            frame(mouse); frame(mouse,0,right,!right); frame(mouse); frame(mouse);
        };
        const auto popup=[]() {
            const auto& stack=ImGui::GetCurrentContext()->OpenPopupStack;
            assert(!stack.empty() && stack.back().Window); return stack.back().Window;
        };
        const auto bottomItem=[&](int indexFromBottom) {
            auto* window=popup(); const auto& style=ImGui::GetStyle();
            return ImVec2{window->Pos.x+30,window->Pos.y+window->Size.y-style.WindowPadding.y-
                ImGui::GetFontSize()*.5f-float(indexFromBottom)*(ImGui::GetFontSize()+style.ItemSpacing.y)};
        };
        click(anchor,true); assert(ImGui::GetCurrentContext()->OpenPopupStack.Size==1);
        click(bottomItem(0)); frame(anchor); assert(Access::Zoom(panel)==1); // Context menu: reset zoom.
        click(anchor,true); click(bottomItem(1)); frame(anchor); // Context menu: frame all.
        assert(Access::Zoom(panel)>=.25f && Access::Zoom(panel)<=2);
        assert(document.Serialize()==viewOnlySnapshot);

        // Add a node at the right-click position after a non-unit zoom/pan.
        const auto spawn=worldAt(anchor); const size_t count=document.Graph().nodes.size();
        click(anchor,true);
        auto* rootMenu=popup();
        const float row=ImGui::GetFontSize()+ImGui::GetStyle().ItemSpacing.y;
        click({rootMenu->Pos.x+30,rootMenu->Pos.y+ImGui::GetStyle().WindowPadding.y+ImGui::GetFontSize()*.5f});
        assert(ImGui::GetCurrentContext()->OpenPopupStack.Size==2);
        auto* addMenu=popup();
        click({addMenu->Pos.x+35,addMenu->Pos.y+ImGui::GetStyle().WindowPadding.y+ImGui::GetFontSize()*.5f+2*row});
        assert(document.Graph().nodes.size()==count+1);
        auto added=document.Graph().nodes.back(); assert(added.kind==PostProcessNodeKind::Vignette);
        assert(std::abs(added.position[0]-spawn.x)<.01f && std::abs(added.position[1]-spawn.y)<.01f);
        frame(anchor);
        auto position=Access::NodePosition(panel,added.id);
        click({position.x+12,position.y+8},true);
        auto* selectedMenu=popup();
        click({selectedMenu->Pos.x+30,selectedMenu->Pos.y+ImGui::GetStyle().WindowPadding.y+ImGui::GetFontSize()*.5f+row});
        assert(document.Graph().nodes.size()==count && !document.Graph().Find(added.id));
        frame(anchor);
        io.AddKeyEvent(ImGuiMod_Ctrl,true); io.AddKeyEvent(ImGuiKey_Z,true); frame(anchor);
        io.AddKeyEvent(ImGuiKey_Z,false); io.AddKeyEvent(ImGuiMod_Ctrl,false); frame(anchor);
        assert(document.Graph().Find(added.id)); // Context deletion is undoable.
        frame(anchor);
        position=Access::NodePosition(panel,added.id);
        const float dragZoom=Access::Zoom(panel);
        const auto originalPosition=document.Graph().Find(added.id)->position;
        const ImVec2 header{position.x+15*dragZoom,position.y+8*dragZoom};
        frame(header); frame(header,0,false,true);
        frame({header.x+40,header.y+25},0,false,true); frame({header.x+40,header.y+25});
        const auto moved=document.Graph().Find(added.id)->position;
        assert(std::abs(moved[0]-originalPosition[0]-40/dragZoom)<2/dragZoom);
        assert(std::abs(moved[1]-originalPosition[1]-25/dragZoom)<2/dragZoom);
        assert(document.Graph().Connect(3,added.id) && document.Graph().Connect(added.id,5));
        frame(anchor); position=Access::NodePosition(panel,added.id);
        click({position.x+12,position.y+8},true);
        auto* disconnectMenu=popup();
        click({disconnectMenu->Pos.x+30,disconnectMenu->Pos.y+ImGui::GetStyle().WindowPadding.y+ImGui::GetFontSize()*.5f+2*row});
        assert(document.Graph().Find(added.id));
        for(const auto& link:document.Graph().links) assert(link.from!=added.id && link.to!=added.id);
    }
    ImGui::DestroyContext();
    std::cout << "PASS: graph validation/persistence, stable node sizes, cursor-anchored zoom/limits, pan/drag at zoom, context add/delete/disconnect/undo and view framing.\n";
}
