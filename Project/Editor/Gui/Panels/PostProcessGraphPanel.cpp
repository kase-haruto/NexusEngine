#include "PostProcessGraphPanel.h"
#include "Editor/Services/LocalizationService.h"
#include "Editor/Core/EditorSession.h"
#include "Foundation/Utility/String/ConvertString.h"
#include "ThirdParty/imnodes/imnodes.h"
#include <cstring>
#include <cmath>
#include <cfloat>

namespace NexusEngine {
    namespace {
        const char* Name(PostProcessNodeKind kind) {
            switch(kind) {
            case PostProcessNodeKind::Scene: return "graph.scene";
            case PostProcessNodeKind::Bloom: return "graph.bloom";
            case PostProcessNodeKind::ToneMapping: return "graph.toneMapping";
            case PostProcessNodeKind::Vignette: return "graph.vignette";
            case PostProcessNodeKind::Shader: return "graph.shader";
            case PostProcessNodeKind::Fxaa: return "graph.fxaa";
            default: return "graph.output";
            }
        }
        bool EditText(const char* label, std::string& text) {
            std::array<char,2048> buffer{};
            std::memcpy(buffer.data(), text.data(), (std::min)(text.size(),buffer.size()-1));
            if(!ImGui::InputText(label,buffer.data(),buffer.size())) return false;
            text=buffer.data(); return true;
        }
        ImVec2 Scaled(ImVec2 v,float scale) { return {v.x*scale,v.y*scale}; }
        struct CanvasScale {
            ImGuiStyle gui=ImGui::GetStyle();
            ImNodesStyle nodes=ImNodes::GetStyle();
            explicit CanvasScale(float zoom) {
                ImGui::GetStyle().ScaleAllSizes(zoom);
                ImGui::PushFont(nullptr,gui.FontSizeBase*zoom);
                auto& s=ImNodes::GetStyle();
                s.GridSpacing*=zoom; s.NodeCornerRounding*=zoom; s.NodePadding=Scaled(s.NodePadding,zoom);
                s.NodeBorderThickness*=zoom; s.LinkThickness*=zoom; s.LinkHoverDistance*=zoom;
                s.PinCircleRadius*=zoom; s.PinQuadSideLength*=zoom; s.PinTriangleSideLength*=zoom;
                s.PinLineThickness*=zoom; s.PinHoverRadius*=zoom; s.PinOffset*=zoom;
                s.MiniMapPadding=Scaled(s.MiniMapPadding,zoom); s.MiniMapOffset=Scaled(s.MiniMapOffset,zoom);
            }
            ~CanvasScale() { ImGui::PopFont(); ImGui::GetStyle()=gui; ImNodes::GetStyle()=nodes; }
        };
    }
    PostProcessGraphPanel::~PostProcessGraphPanel() { if(nodes_) ImNodes::DestroyContext(nodes_); }
    void PostProcessGraphPanel::SetZoom(float value,const std::array<float,2>& anchor) {
        if(!std::isfinite(value)) return;
        value=std::clamp(value,0.25f,2.0f);
        if(value==zoom_) return;
        const auto pan=ImNodes::EditorContextGetPanning();
        const float ratio=value/zoom_;
        ImNodes::EditorContextResetPanning({anchor[0]-(anchor[0]-pan.x)*ratio,anchor[1]-(anchor[1]-pan.y)*ratio});
        zoom_=value; positioned_.clear();
    }
    void PostProcessGraphPanel::FrameAll() {
        if(document_.Graph().nodes.empty() || canvasSize_[0]<=0 || canvasSize_[1]<=0) return;
        ImVec2 minimum{FLT_MAX,FLT_MAX}, maximum{-FLT_MAX,-FLT_MAX};
        for(const auto& node:document_.Graph().nodes) {
            if(!positioned_.contains(node.id)) continue;
            const auto size=ImNodes::GetNodeDimensions(node.id);
            minimum.x=std::min(minimum.x,node.position[0]); minimum.y=std::min(minimum.y,node.position[1]);
            maximum.x=std::max(maximum.x,node.position[0]+size.x/zoom_); maximum.y=std::max(maximum.y,node.position[1]+size.y/zoom_);
        }
        if(minimum.x==FLT_MAX) return;
        const float fit=std::min((canvasSize_[0]-64)/std::max(maximum.x-minimum.x,1.0f),
            (canvasSize_[1]-64)/std::max(maximum.y-minimum.y,1.0f));
        zoom_=std::clamp(fit,0.25f,2.0f);
        ImNodes::EditorContextResetPanning({canvasSize_[0]*0.5f-(minimum.x+maximum.x)*0.5f*zoom_,
            canvasSize_[1]*0.5f-(minimum.y+maximum.y)*0.5f*zoom_});
        positioned_.clear();
    }
    void PostProcessGraphPanel::Draw(const EditorContext& context) {
        auto* previous = ImNodes::GetCurrentContext();
        if(!nodes_) nodes_ = ImNodes::CreateContext();
        ImNodes::SetCurrentContext(nodes_);
        ImNodes::GetIO().LinkDetachWithModifierClick.Modifier = &ImGui::GetIO().KeyCtrl;
        auto& graph = document_.Graph(); const auto before = graph;
        const bool editable=!context.session.IsPlaying();
        bool changed = false, remove = false, frameAll = false, resetZoom = false;
        const auto& text = context.text;
        const auto addNode=[&](PostProcessNodeKind kind,const std::array<float,2>& position) {
            int id=1; for(const auto& node:graph.nodes) id=std::max(id,node.id+1);
            PostProcessNode node; node.id=id; node.kind=kind; node.position=position;
            if(kind==PostProcessNodeKind::Vignette) node.effect.parameters[0]=0.5f;
            graph.nodes.push_back(std::move(node)); changed=true;
        };
        const auto undo=[&] {
            if(undo_.empty()) return;
            redo_.push_back(graph); graph=std::move(undo_.back()); undo_.pop_back(); positioned_.clear(); document_.Changed();
        };
        const auto redo=[&] {
            if(redo_.empty()) return;
            undo_.push_back(graph); graph=std::move(redo_.back()); redo_.pop_back(); positioned_.clear(); document_.Changed();
        };
        if(ImGui::Button(text.Text("graph.add").c_str())) ImGui::OpenPopup("AddNode");
        if(ImGui::BeginPopup("AddNode")) {
            for(auto kind : {PostProcessNodeKind::Bloom,PostProcessNodeKind::ToneMapping,PostProcessNodeKind::Vignette,
                            PostProcessNodeKind::Shader,PostProcessNodeKind::Fxaa}) {
                if(ImGui::MenuItem(text.Text(Name(kind)).c_str())) {
                    const auto pan=ImNodes::EditorContextGetPanning();
                    addNode(kind,{(80-pan.x)/zoom_,(80-pan.y)/zoom_});
                }
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine(); remove = ImGui::Button(text.Text("graph.remove").c_str());
        ImGui::SameLine(); if(ImGui::Button(text.Text("graph.apply").c_str())) document_.RequestApply();
        ImGui::SameLine(); ImGui::Checkbox(text.Text("graph.autoApply").c_str(), &document_.autoApply);
        if(document_.autoApply && ImGui::IsItemEdited()) document_.RequestApply();
        if(ImGui::Button(text.Text("graph.save").c_str())) { auto saved=document_.Save(); if(!saved) document_.Applied(saved); }
        ImGui::SameLine(); if(ImGui::Button(text.Text("graph.reloadShaders").c_str())) document_.reloadRequested=true;
        ImGui::SameLine(); if(ImGui::Button(text.Text("graph.load").c_str())) {
            auto loaded=document_.Load(); document_.Applied(loaded); if(loaded) { positioned_.clear(); undo_.clear(); redo_.clear(); }
        }
        ImGui::SameLine(); if(ImGui::Button(text.Text("graph.reset").c_str())) { graph=PostProcessGraph::Default(); positioned_.clear(); changed=true; }
        ImGui::SameLine(); ImGui::BeginDisabled(undo_.empty());
        if(ImGui::Button(text.Text("graph.undo").c_str())) {
            undo();
        }
        ImGui::EndDisabled(); ImGui::SameLine(); ImGui::BeginDisabled(redo_.empty());
        if(ImGui::Button(text.Text("graph.redo").c_str())) {
            redo();
        }
        ImGui::EndDisabled();
        ImGui::SameLine(); if(ImGui::Button(text.Text("graph.frameAll").c_str())) frameAll=true;
        ImGui::SameLine(); if(ImGui::Button("100%")) resetZoom=true;
        ImGui::SameLine(); ImGui::Text("%.0f%%",zoom_*100);
        if(document_.IsDirty()) { ImGui::SameLine(); ImGui::TextUnformatted("*"); }
        ImGui::TextDisabled("%s",text.Text("graph.help").c_str());
        if(!document_.Status().empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, document_.HasError()?ImVec4(1,.45f,.35f,1):ImVec4(.5f,.8f,.55f,1));
            ImGui::TextWrapped("%s",document_.Status().c_str()); ImGui::PopStyleColor();
        }
        const float drawZoom=zoom_;
        bool canvasHovered=false;
        {
        CanvasScale scale(drawZoom);
        ImNodes::PushAttributeFlag(ImNodesAttributeFlags_EnableLinkDetachWithDragClick);
        ImNodes::BeginNodeEditor();
        auto origin=ImGui::GetCursorScreenPos(), size=ImGui::GetWindowSize();
        canvasOrigin_={origin.x,origin.y}; canvasSize_={size.x,size.y};
        for(auto& node : graph.nodes) {
            if(positioned_.insert(node.id).second) ImNodes::SetNodeGridSpacePos(node.id,{node.position[0]*drawZoom,node.position[1]*drawZoom});
            const bool hdr = PostProcessGraph::OutputsHdr(node.kind);
            ImNodes::PushColorStyle(ImNodesCol_TitleBar,hdr ? IM_COL32(105,70,35,255) : IM_COL32(35,72,100,255));
            ImNodes::BeginNode(node.id); ImGui::PushID(node.id);
            ImNodes::SetNodeDraggable(node.id,editable);
            ImNodes::BeginNodeTitleBar(); ImGui::TextUnformatted(text.Text(Name(node.kind)).c_str()); ImNodes::EndNodeTitleBar();
            if(node.kind != PostProcessNodeKind::Scene) {
                ImNodes::BeginInputAttribute(node.id*3);
                ImGui::TextUnformatted(PostProcessGraph::InputsHdr(node.kind)?"HDR":"Color"); ImNodes::EndInputAttribute();
            }
            // An empty ImGui group can inherit the title's previous-frame rectangle.
            // With imnodes' padded title this feeds its old width back into node sizing.
            // Source/output nodes have no controls, so do not emit an empty attribute.
            if(node.kind!=PostProcessNodeKind::Scene && node.kind!=PostProcessNodeKind::Output) {
            ImNodes::BeginStaticAttribute(node.id*3+2); ImGui::PushItemWidth(195*drawZoom);
            if(node.kind!=PostProcessNodeKind::Scene && node.kind!=PostProcessNodeKind::Output && node.kind!=PostProcessNodeKind::ToneMapping)
                changed |= ImGui::Checkbox(text.Text("graph.enabled").c_str(),&node.enabled);
            auto& s=graph.settings;
            switch(node.kind) {
            case PostProcessNodeKind::Bloom:
                changed|=ImGui::SliderFloat(text.Text("graph.intensity").c_str(),&s.bloomIntensity,0,2);
                changed|=ImGui::DragFloat(text.Text("graph.threshold").c_str(),&s.bloomThreshold,.05f,0,65504);
                changed|=ImGui::SliderFloat(text.Text("graph.knee").c_str(),&s.bloomSoftKnee,0,1); break;
            case PostProcessNodeKind::ToneMapping: {
                int mapper=int(s.GetEffectiveToneMapper());
                if(ImGui::Combo(text.Text("graph.mapper").c_str(),&mapper,"Linear Clamp\0ACES Fitted\0PBR Neutral\0")) { s.toneMapper=static_cast<ToneMapper>(mapper); s.filmicEnabled=true; changed=true; }
                changed|=ImGui::SliderFloat(text.Text("graph.exposure").c_str(),&s.exposureEV,-20,20);
                changed|=ImGui::SliderFloat(text.Text("graph.saturation").c_str(),&s.saturation,0,4);
                changed|=ImGui::SliderFloat(text.Text("graph.contrast").c_str(),&s.contrast,.01f,4);
                changed|=ImGui::SliderFloat(text.Text("graph.gamma").c_str(),&s.gamma,.01f,4); break;
            }
            case PostProcessNodeKind::Vignette:
                changed|=ImGui::SliderFloat(text.Text("graph.strength").c_str(),&node.effect.parameters[0],0,1); break;
            case PostProcessNodeKind::Shader: {
                std::string path=ConvertString(node.effect.shaderPath.wstring()), entry=ConvertString(node.effect.entryPoint);
                if(EditText(text.Text("graph.path").c_str(),path)) { node.effect.shaderPath=ConvertString(path); changed=true; }
                if(EditText(text.Text("graph.entry").c_str(),entry)) { node.effect.entryPoint=ConvertString(entry); changed=true; }
                for(int row=0;row<4;++row) { ImGui::PushID(row); changed|=ImGui::DragFloat4("float4",node.effect.parameters.data()+row*4,.01f); ImGui::PopID(); }
                break;
            }
            default: break;
            }
            ImGui::PopItemWidth(); ImNodes::EndStaticAttribute();
            }
            if(node.kind != PostProcessNodeKind::Output) {
                ImNodes::BeginOutputAttribute(node.id*3+1); ImGui::TextUnformatted(hdr?"HDR":"Color"); ImNodes::EndOutputAttribute();
            }
            ImGui::PopID(); ImNodes::EndNode(); ImNodes::PopColorStyle();
        }
        for(const auto& link:graph.links) ImNodes::Link(link.to,link.from*3+1,link.to*3);
        ImNodes::MiniMap(0.15f,ImNodesMiniMapLocation_BottomRight);
        canvasHovered=ImGui::IsWindowHovered() && !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId);
        ImNodes::EndNodeEditor(); ImNodes::PopAttributeFlag();
        }
        document_.capturesKeyboard = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        int start=0,end=0,destroyed=0;
        if(editable && ImNodes::IsLinkCreated(&start,&end)) {
            if(start%3==0) std::swap(start,end);
            if(start%3==1 && end%3==0) {
                auto connected=graph.Connect(start/3,end/3); if(!connected) document_.Applied(connected); else changed=true;
            }
        }
        if(editable && ImNodes::IsLinkDestroyed(&destroyed)) { std::erase_if(graph.links,[&](auto l){return l.to==destroyed;}); changed=true; }
        if(editable && document_.capturesKeyboard && !ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl) {
            const bool redoing=ImGui::GetIO().KeyShift || ImGui::IsKeyPressed(ImGuiKey_Y,false);
            if(ImGui::IsKeyPressed(ImGuiKey_Z,false) || ImGui::IsKeyPressed(ImGuiKey_Y,false)) {
                if(redoing) redo(); else undo();
            }
            if(ImGui::IsKeyPressed(ImGuiKey_S,false)) { auto saved=document_.Save(); if(!saved) document_.Applied(saved); }
        }
        if(canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !ImGui::IsAnyItemActive()) {
            const auto mouse=ImGui::GetMousePos(), pan=ImNodes::EditorContextGetPanning();
            contextPosition_={(mouse.x-canvasOrigin_[0]-pan.x)/drawZoom,(mouse.y-canvasOrigin_[1]-pan.y)/drawZoom};
            contextNode_=0; int link=0;
            if(ImNodes::IsNodeHovered(&contextNode_)) {
                if(!ImNodes::IsNodeSelected(contextNode_)) { ImNodes::ClearNodeSelection(); ImNodes::ClearLinkSelection(); ImNodes::SelectNode(contextNode_); }
            } else if(ImNodes::IsLinkHovered(&link)) {
                if(!ImNodes::IsLinkSelected(link)) { ImNodes::ClearNodeSelection(); ImNodes::ClearLinkSelection(); ImNodes::SelectLink(link); }
            }
            ImGui::OpenPopup("GraphContext");
        }
        if(ImGui::BeginPopup("GraphContext")) {
            ImGui::BeginDisabled(!editable);
            if(ImGui::BeginMenu(text.Text("graph.add").c_str())) {
                for(auto kind:{PostProcessNodeKind::Bloom,PostProcessNodeKind::ToneMapping,PostProcessNodeKind::Vignette,PostProcessNodeKind::Shader,PostProcessNodeKind::Fxaa})
                    if(ImGui::MenuItem(text.Text(Name(kind)).c_str())) addNode(kind,contextPosition_);
                ImGui::EndMenu();
            }
            auto* clicked=graph.Find(contextNode_);
            const bool canDelete=ImNodes::NumSelectedLinks()>0 || [&] {
                std::vector<int> selection(static_cast<size_t>(ImNodes::NumSelectedNodes()));
                if(!selection.empty()) ImNodes::GetSelectedNodes(selection.data());
                for(int id:selection) { const auto* n=graph.Find(id); if(n && n->kind!=PostProcessNodeKind::Scene && n->kind!=PostProcessNodeKind::Output) return true; }
                return false;
            }();
            if(ImGui::MenuItem(text.Text("graph.remove").c_str(),"Delete",false,canDelete)) remove=true;
            const bool connected=clicked && std::any_of(graph.links.begin(),graph.links.end(),[&](auto l){return l.from==contextNode_||l.to==contextNode_;});
            if(ImGui::MenuItem(text.Text("graph.disconnect").c_str(),nullptr,false,connected)) {
                std::erase_if(graph.links,[&](auto l){return l.from==contextNode_||l.to==contextNode_;}); changed=true;
            }
            ImGui::Separator();
            if(ImGui::MenuItem(text.Text("graph.undo").c_str(),"Ctrl+Z",false,!undo_.empty())) undo();
            if(ImGui::MenuItem(text.Text("graph.redo").c_str(),"Ctrl+Shift+Z",false,!redo_.empty())) redo();
            ImGui::EndDisabled(); ImGui::Separator();
            if(ImGui::MenuItem(text.Text("graph.frameAll").c_str())) frameAll=true;
            if(ImGui::MenuItem(text.Text("graph.resetZoom").c_str())) resetZoom=true;
            ImGui::EndPopup();
        }
        remove |= document_.capturesKeyboard && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete,false);
        if(editable && remove) {
            std::vector<int> selected(static_cast<size_t>(ImNodes::NumSelectedNodes()));
            if(!selected.empty()) ImNodes::GetSelectedNodes(selected.data());
            for(int id:selected) {
                const auto* node=graph.Find(id);
                if(node && node->kind!=PostProcessNodeKind::Scene && node->kind!=PostProcessNodeKind::Output) {
                    std::erase_if(graph.nodes,[&](auto& n){return n.id==id;});
                    std::erase_if(graph.links,[&](auto l){return l.from==id||l.to==id;}); positioned_.erase(id); changed=true;
                }
            }
            selected.resize(static_cast<size_t>(ImNodes::NumSelectedLinks()));
            if(!selected.empty()) ImNodes::GetSelectedLinks(selected.data());
            for(int id:selected) { std::erase_if(graph.links,[&](auto l){return l.to==id;}); changed=true; }
            ImNodes::ClearNodeSelection(); ImNodes::ClearLinkSelection();
        }
        for(auto& node:graph.nodes) if(positioned_.contains(node.id)) {
            auto p=ImNodes::GetNodeGridSpacePos(node.id);
            const std::array<float,2> position{p.x/drawZoom,p.y/drawZoom};
            if(std::abs(node.position[0]-position[0])>0.01f || std::abs(node.position[1]-position[1])>0.01f) { node.position=position; document_.Changed(false); }
        }
        if(frameAll) FrameAll();
        else if(resetZoom) SetZoom(1,{canvasSize_[0]*0.5f,canvasSize_[1]*0.5f});
        else if(canvasHovered && !ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Middle) && !ImGui::IsMouseDown(ImGuiMouseButton_Right) && ImGui::GetIO().MouseWheel!=0) {
            const auto mouse=ImGui::GetMousePos();
            SetZoom(zoom_*std::pow(1.15f,std::clamp(ImGui::GetIO().MouseWheel,-20.0f,20.0f)),{mouse.x-canvasOrigin_[0],mouse.y-canvasOrigin_[1]});
        }
        if(changed) { undo_.push_back(before); if(undo_.size()>64) undo_.erase(undo_.begin()); redo_.clear(); document_.Changed(); }
        ImNodes::SetCurrentContext(previous);
    }
}
