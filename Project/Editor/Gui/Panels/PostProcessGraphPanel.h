#pragma once
#include "IEditorPanel.h"
#include "Editor/Services/PostProcessGraphDocument.h"
#include <set>
struct ImNodesContext;
namespace NexusEngine {
    class PostProcessGraphPanel final : public IEditorPanel {
    public:
        explicit PostProcessGraphPanel(PostProcessGraphDocument& document) : document_(document) {}
        ~PostProcessGraphPanel() override;
        std::string_view Id() const noexcept override { return "PostProcessGraph"; }
        std::string_view TitleKey() const noexcept override { return "panel.postProcessGraph"; }
        bool IsScrollable() const noexcept override { return false; }
        std::array<float,2> InitialSize() const noexcept override { return {1100,620}; }
        void Draw(const EditorContext&) override;
    private:
        friend struct PostProcessGraphPanelTestAccess;
        void SetZoom(float value, const std::array<float,2>& anchor);
        void FrameAll();
        PostProcessGraphDocument& document_;
        ImNodesContext* nodes_ = nullptr;
        std::set<int> positioned_;
        std::vector<PostProcessGraph> undo_, redo_;
        float zoom_ = 1;
        std::array<float,2> canvasOrigin_{}, canvasSize_{}, contextPosition_{};
        int contextNode_ = 0;
    };
}
