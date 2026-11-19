#pragma once
#include "Runtime/Animation/AnimationPlayback.h"
#include "Graphics/Model/PoseBlendMode.h"
#include <filesystem>
#include <map>
#include <memory>
#include <string>

namespace NexusEngine {
struct AnimationLayerDefinition {
    std::string name = "Layer", clip, speedCurve, weightCurve;
    float weight = 1, speed = 1, curveDuration = 0;
    bool enabled = true, loop = true;
    PoseBlendMode mode = PoseBlendMode::Override;
    std::vector<std::string> mask; //< target階層path。指定jointの子孫も対象。空は全体
    bool operator==(const AnimationLayerDefinition&) const;
};
struct AnimationLayerDocument {
    std::vector<AnimationLayerDefinition> layers; //< 順序が混合結果を決める
    bool operator==(const AnimationLayerDocument&) const;
};
struct AnimationLayerPlayback {
    AnimationPlayback clock;
    float elapsed = 0; //< Entityごとの独立経過秒
};
Result<void> ValidateAnimationLayers(const AnimationLayerDocument& document);
Result<AnimationLayerDocument> LoadAnimationLayers(const std::filesystem::path& file);
Result<void> SaveAnimationLayers(const std::filesystem::path& file, const AnimationLayerDocument& document);
/** \brief Layer定義だけの共有cache。個体のclock/poseは含めない。 */
class AnimationLayerAssets final {
public:
    void SetRoot(const std::filesystem::path& root);
    void Invalidate();
    Result<std::shared_ptr<const AnimationLayerDocument>> Load(const std::string& reference);
private:
    std::filesystem::path root_;
    std::map<std::filesystem::path, std::shared_ptr<const AnimationLayerDocument>> saved_;
};
}
