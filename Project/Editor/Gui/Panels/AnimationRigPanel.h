#pragma once
#include "IEditorPanel.h"
#include "Editor/Services/AnimationRigDocument.h"
#include <future>

namespace NexusEngine {
/** \brief Retarget mappingと順序付きlayerのasset編集。モデル読込だけをworkerへ渡す。 */
class AnimationRigPanel final : public IEditorPanel {
public:
    AnimationRigPanel(AnimationRigDocument& document, std::filesystem::path root);
    std::string_view Id() const noexcept override;
    std::string_view TitleKey() const noexcept override;
    std::array<float, 2> InitialSize() const noexcept override;
    void Draw(const EditorContext& context) override;
private:
    using Models = std::pair<std::shared_ptr<const ModelAssetData>, std::shared_ptr<const ModelAssetData>>;
    void LoadModels(const std::string& source, const std::string& target);
    void DrawRetarget(RetargetProfile& profile);
    void DrawLayers(AnimationLayerDocument& layers);
    void Report(const Result<void>& result);
    AnimationRigDocument& document_;
    std::filesystem::path root_, pendingOpen_;
    int pendingNew_ = -1;
    std::array<char, 2048> savePath_{};
    std::string shownReference_, status_, pendingSave_, maskModel_, loadedSource_, loadedTarget_;
    std::future<Result<Models>> loading_;
    Models models_;
};
}
