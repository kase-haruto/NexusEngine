#pragma once
#include "Runtime/Animation/RetargetProfile.h"
#include "Runtime/Animation/AnimationLayers.h"
#include "EditorHistory.h"
#include <optional>
#include <variant>

namespace NexusEngine {
/** \brief Rig/Layer assetの編集寿命。共通History/Serializerを使い、SceneやGPUを所有しない。 */
class AnimationRigDocument final {
public:
    using Value = std::variant<RetargetProfile, AnimationLayerDocument>;
    void Initialize(const std::filesystem::path& root);
    void New(bool layers);
    Result<void> Open(const std::filesystem::path& file);
    Result<void> Save(const std::string& reference);
    void BeginEdit();
    void EndEdit();
    bool Undo();
    bool Redo();
    bool IsDirty() const;
    const std::string& Reference() const;
    Value& Draft();
    const Value& Draft() const;
    void RefreshLocation();
    size_t SavedRevision() const noexcept;
    bool focused = false; //< 他文書のglobal shortcutへ同じ入力を渡さない
private:
    std::filesystem::path root_;
    std::string reference_ = "Rigs/NewRig.nxretarget";
    std::string identity_;
    Value draft_;
    std::optional<Value> before_;
    EditorHistory history_;
    bool fresh_ = false;
    size_t savedRevision_ = 0; //< 保存後のRuntime/model cache更新に使う。assetには保存しない
};
}
