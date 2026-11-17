#include "NodeEditor.h"
#include "ThirdParty/imnodes/imnodes.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace NexusEngine::UI {
struct NodeEditor::Impl {
    ImNodesContext* context = nullptr;
    ImNodesContext* previous = nullptr;
    ImGuiStyle gui;
    ImNodesStyle nodes;
    bool session = false, canvas = false;
};
NodeEditor::NodeEditor() : impl_(std::make_unique<Impl>()) {
}
NodeEditor::~NodeEditor() {
    if(impl_->canvas) {
        EndCanvas();
    }
    if(impl_->session) {
        EndSession();
    }
    if(impl_->context) {
        ImNodes::DestroyContext(impl_->context);
    }
}
void NodeEditor::BeginSession() {
    if(impl_->session) {
        throw std::logic_error("A node editor session cannot be reentered.");
    }
    impl_->previous = ImNodes::GetCurrentContext();
    if(!impl_->context) {
        impl_->context = ImNodes::CreateContext();
    }
    ImNodes::SetCurrentContext(impl_->context);
    ImNodes::GetIO().LinkDetachWithModifierClick.Modifier = &ImGui::GetIO().KeyCtrl;
    impl_->session = true;
}
void NodeEditor::EndSession() {
    if(impl_->canvas) {
        EndCanvas();
    }
    ImNodes::SetCurrentContext(impl_->previous);
    impl_->session = false;
}
void NodeEditor::BeginCanvas(float zoom) {
    if(!impl_->session || impl_->canvas || !std::isfinite(zoom) || zoom <= 0) {
        throw std::logic_error("A node canvas requires an active session and positive zoom.");
    }
    impl_->gui = ImGui::GetStyle();
    impl_->nodes = ImNodes::GetStyle();
    ImGui::GetStyle().ScaleAllSizes(zoom);
    ImGui::PushFont(nullptr, impl_->gui.FontSizeBase * zoom);
    auto& style = ImNodes::GetStyle();
    style.GridSpacing *= zoom;
    style.NodeCornerRounding *= zoom;
    style.NodePadding = {style.NodePadding.x*zoom, style.NodePadding.y*zoom};
    style.NodeBorderThickness *= zoom;
    style.LinkThickness *= zoom;
    style.LinkHoverDistance *= zoom;
    style.PinCircleRadius *= zoom;
    style.PinQuadSideLength *= zoom;
    style.PinTriangleSideLength *= zoom;
    style.PinLineThickness *= zoom;
    style.PinHoverRadius *= zoom;
    style.PinOffset *= zoom;
    style.MiniMapPadding = {style.MiniMapPadding.x*zoom, style.MiniMapPadding.y*zoom};
    style.MiniMapOffset = {style.MiniMapOffset.x*zoom, style.MiniMapOffset.y*zoom};
    ImNodes::PushAttributeFlag(ImNodesAttributeFlags_EnableLinkDetachWithDragClick);
    ImGui::PushID(impl_->context);
    ImNodes::BeginNodeEditor();
    impl_->canvas = true;
}
void NodeEditor::EndCanvas() {
    ImNodes::EndNodeEditor();
    ImGui::PopID();
    ImNodes::PopAttributeFlag();
    ImGui::PopFont();
    ImGui::GetStyle() = impl_->gui;
    ImNodes::GetStyle() = impl_->nodes;
    impl_->canvas = false;
}
void NodeEditor::BeginNode(int id) {
    ImNodes::BeginNode(id);
}
void NodeEditor::EndNode() {
    ImNodes::EndNode();
}
void NodeEditor::BeginTitle() {
    ImNodes::BeginNodeTitleBar();
}
void NodeEditor::EndTitle() {
    ImNodes::EndNodeTitleBar();
}
void NodeEditor::BeginInput(int id) {
    ImNodes::BeginInputAttribute(id);
}
void NodeEditor::EndInput() {
    ImNodes::EndInputAttribute();
}
void NodeEditor::BeginOutput(int id) {
    ImNodes::BeginOutputAttribute(id);
}
void NodeEditor::EndOutput() {
    ImNodes::EndOutputAttribute();
}
void NodeEditor::BeginStatic(int id) {
    ImNodes::BeginStaticAttribute(id);
}
void NodeEditor::EndStatic() {
    ImNodes::EndStaticAttribute();
}
void NodeEditor::PushTitleColor(uint32_t color) {
    ImNodes::PushColorStyle(ImNodesCol_TitleBar, color);
}
void NodeEditor::PopTitleColor() {
    ImNodes::PopColorStyle();
}
void NodeEditor::SetDraggable(int id, bool enabled) {
    ImNodes::SetNodeDraggable(id, enabled);
}
void NodeEditor::Link(int id, int output, int input) {
    ImNodes::Link(id, output, input);
}
bool NodeEditor::LinkCreated(int& output, int& input) const {
    return ImNodes::IsLinkCreated(&output, &input);
}
bool NodeEditor::LinkDestroyed(int& id) const {
    return ImNodes::IsLinkDestroyed(&id);
}
bool NodeEditor::HoveredNode(int& id) const {
    return ImNodes::IsNodeHovered(&id);
}
bool NodeEditor::HoveredLink(int& id) const {
    return ImNodes::IsLinkHovered(&id);
}
bool NodeEditor::NodeSelected(int id) const {
    return ImNodes::IsNodeSelected(id);
}
bool NodeEditor::LinkSelected(int id) const {
    return ImNodes::IsLinkSelected(id);
}
void NodeEditor::SelectNode(int id) {
    if(!ImNodes::IsNodeSelected(id)) {
        ImNodes::SelectNode(id);
    }
}
void NodeEditor::SelectLink(int id) {
    if(!ImNodes::IsLinkSelected(id)) {
        ImNodes::SelectLink(id);
    }
}
void NodeEditor::ClearSelection() {
    ImNodes::ClearNodeSelection();
    ImNodes::ClearLinkSelection();
}
std::vector<int> NodeEditor::SelectedNodes() const {
    std::vector<int> result(static_cast<size_t>(ImNodes::NumSelectedNodes()));
    if(!result.empty()) {
        ImNodes::GetSelectedNodes(result.data());
    }
    return result;
}
std::vector<int> NodeEditor::SelectedLinks() const {
    std::vector<int> result(static_cast<size_t>(ImNodes::NumSelectedLinks()));
    if(!result.empty()) {
        ImNodes::GetSelectedLinks(result.data());
    }
    return result;
}
void NodeEditor::SetNodePosition(int id, std::array<float, 2> position) {
    ImNodes::SetNodeGridSpacePos(id, {position[0],position[1]});
}
std::array<float, 2> NodeEditor::NodePosition(int id) const {
    const auto value = ImNodes::GetNodeGridSpacePos(id);
    return {value.x,value.y};
}
std::array<float, 2> NodeEditor::NodeScreenPosition(int id) const {
    const auto value = ImNodes::GetNodeScreenSpacePos(id);
    return {value.x,value.y};
}
std::array<float, 2> NodeEditor::NodeDimensions(int id) const {
    const auto value = ImNodes::GetNodeDimensions(id);
    return {value.x,value.y};
}
std::array<float, 2> NodeEditor::Panning() const {
    const auto value = ImNodes::EditorContextGetPanning();
    return {value.x,value.y};
}
void NodeEditor::SetPanning(std::array<float, 2> value) {
    ImNodes::EditorContextResetPanning({value[0],value[1]});
}
float NodeEditor::ZoomAt(float current, float requested, std::array<float, 2> anchor) {
    if(!std::isfinite(requested) || !std::isfinite(current) || current <= 0) {
        return current;
    }
    const float zoom = std::clamp(requested,.25f,2.0f);
    const auto pan = Panning();
    const float ratio = zoom/current;
    SetPanning({anchor[0]-(anchor[0]-pan[0])*ratio,anchor[1]-(anchor[1]-pan[1])*ratio});
    return zoom;
}
void NodeEditor::MiniMap(float fraction) {
    ImNodes::MiniMap(fraction, ImNodesMiniMapLocation_BottomRight);
}
float NodeEditor::FrameAll(const std::vector<Layout>& nodes, std::array<float, 2> size, float zoom) {
    if(nodes.empty() || size[0] <= 0 || size[1] <= 0 || zoom <= 0) {
        return zoom;
    }
    std::array<float, 2> minimum{(std::numeric_limits<float>::max)(), (std::numeric_limits<float>::max)()};
    std::array<float, 2> maximum{-(std::numeric_limits<float>::max)(), -(std::numeric_limits<float>::max)()};
    for(const auto& node : nodes) {
        const auto dimensions = NodeDimensions(node.id);
        for(size_t axis = 0; axis < 2; ++axis) {
            minimum[axis] = (std::min)(minimum[axis], node.position[axis]);
            maximum[axis] = (std::max)(maximum[axis], node.position[axis] + dimensions[axis]/zoom);
        }
    }
    const float fit = (std::min)((size[0]-64)/(std::max)(maximum[0]-minimum[0],1.0f),
        (size[1]-64)/(std::max)(maximum[1]-minimum[1],1.0f));
    const float result = std::clamp(fit,.25f,2.0f);
    SetPanning({size[0]*.5f-(minimum[0]+maximum[0])*.5f*result,
        size[1]*.5f-(minimum[1]+maximum[1])*.5f*result});
    return result;
}
ImNodesContext* NodeEditor::NativeContext() const noexcept {
    return impl_->context;
}
NodeEditorSession::NodeEditorSession(NodeEditor& editor) : editor_(editor) {
    editor_.BeginSession();
}
NodeEditorSession::~NodeEditorSession() {
    editor_.EndSession();
}
NodeEditorCanvas::NodeEditorCanvas(NodeEditor& editor, float zoom) : editor_(editor) {
    editor_.BeginCanvas(zoom);
}
NodeEditorCanvas::~NodeEditorCanvas() {
    Close();
}
void NodeEditorCanvas::Close() {
    if(open_) {
        editor_.EndCanvas();
        open_ = false;
    }
}
}
