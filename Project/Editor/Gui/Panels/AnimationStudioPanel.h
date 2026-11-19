#pragma once
#include "IEditorPanel.h"
#include "Editor/Services/AnimationStudioDocument.h"
#include "Editor/Gui/Nodes/NodeEditor.h"
#include <set>

namespace NexusEngine {
/** 同じアセットをClip/Animator/Timelineの各編集画面から扱う。 */
class AnimationStudioPanel final : public IEditorPanel {
public:
    explicit AnimationStudioPanel(AnimationStudioDocument& document);
    std::string_view Id() const noexcept override;
    std::string_view TitleKey() const noexcept override;
    std::array<float, 2> InitialSize() const noexcept override;
    void Draw(const EditorContext& context) override;
private:
    void DrawClip(const EditorContext& context, AnimationClipDocument& clip, bool& changed);
    void DrawController(const EditorContext& context, AnimatorDocument& controller, bool& changed);
    void DrawControllerGraph(const EditorContext& context, AnimatorDocument& controller, bool& changed);
    void DrawTimeline(const EditorContext& context, TimelineDocument& timeline, bool& changed);
    void DrawKeys(AnimationClipDocument& clip, bool& changed);
    void DrawCurve(const AnimationTrack& track, float duration);
    bool AssetField(const char* label, std::string& value, const char* extension);
    AnimationStudioDocument& document_;
    int pendingNew_ = -1;
    float zoom_ = 1;
    int morphChannelStart_ = 0; //< 大きなweight配列も4channelずつ編集・描画する
    UI::NodeEditor nodeEditor_;
    float nodeZoom_ = 1;
    std::string graphReference_;
    std::vector<std::string> graphStateNames_;
    std::set<std::string> collapsedTracks_; //< timelineのComponent階層。assetには保存しない
};
}
