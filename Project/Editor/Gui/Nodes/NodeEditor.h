#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

struct ImNodesContext;
namespace NexusEngine::UI {
/** ImNodesの所有・操作を共通化する。ノードの意味、接続検証、保存とUndoは各Documentが持つ。 */
class NodeEditor final {
public:
    struct Layout {
        int id;
        std::array<float, 2> position;
    };
    NodeEditor();
    ~NodeEditor();
    NodeEditor(const NodeEditor&) = delete;
    NodeEditor& operator=(const NodeEditor&) = delete;
    void BeginSession();
    void EndSession();
    void BeginCanvas(float zoom = 1);
    void EndCanvas();
    void BeginNode(int id);
    void EndNode();
    void BeginTitle();
    void EndTitle();
    void BeginInput(int id);
    void EndInput();
    void BeginOutput(int id);
    void EndOutput();
    void BeginStatic(int id);
    void EndStatic();
    void PushTitleColor(uint32_t color);
    void PopTitleColor();
    void SetDraggable(int id, bool enabled);
    void Link(int id, int output, int input);
    bool LinkCreated(int& output, int& input) const;
    bool LinkDestroyed(int& id) const;
    bool HoveredNode(int& id) const;
    bool HoveredLink(int& id) const;
    bool NodeSelected(int id) const;
    bool LinkSelected(int id) const;
    void SelectNode(int id);
    void SelectLink(int id);
    void ClearSelection();
    std::vector<int> SelectedNodes() const;
    std::vector<int> SelectedLinks() const;
    void SetNodePosition(int id, std::array<float, 2> position);
    std::array<float, 2> NodePosition(int id) const;
    std::array<float, 2> NodeScreenPosition(int id) const;
    std::array<float, 2> NodeDimensions(int id) const;
    std::array<float, 2> Panning() const;
    void SetPanning(std::array<float, 2> value);
    /** アンカー位置の論理座標を維持するズーム。両パネルから同じ制約で使用する。 */
    float ZoomAt(float current, float requested, std::array<float, 2> anchor);
    float FrameAll(const std::vector<Layout>& nodes, std::array<float, 2> size, float zoom);
    void MiniMap(float fraction = .15f);
    /** テスト・診断専用。製品のノード機能からImNodesへ直接依存しない。 */
    ImNodesContext* NativeContext() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
/** 例外や途中returnでも以前のImNodes contextへ必ず戻す。 */
class NodeEditorSession final {
public:
    explicit NodeEditorSession(NodeEditor& editor);
    ~NodeEditorSession();
    NodeEditorSession(const NodeEditorSession&) = delete;
    NodeEditorSession& operator=(const NodeEditorSession&) = delete;
private:
    NodeEditor& editor_;
};
/** canvas内だけstyle/fontを倍率変更し、他パネルへの漏出を防ぐ。 */
class NodeEditorCanvas final {
public:
    NodeEditorCanvas(NodeEditor& editor, float zoom = 1);
    ~NodeEditorCanvas();
    void Close();
    NodeEditorCanvas(const NodeEditorCanvas&) = delete;
    NodeEditorCanvas& operator=(const NodeEditorCanvas&) = delete;
private:
    NodeEditor& editor_;
    bool open_ = true;
};
}
