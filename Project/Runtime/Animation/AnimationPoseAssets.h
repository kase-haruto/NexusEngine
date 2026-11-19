#pragma once
#include "AnimationClipAssets.h"
#include "Graphics/Model/AnimationAsset.h"
#include "Graphics/Model/ModelAssetData.h"

namespace NexusEngine {
/** \brief authored $node clipの共通変換。描画とRoot Motionで異なる解釈を作らない。 */
Result<AnimationAsset> BuildAuthoredAnimation(const std::string& reference, const AnimationClipDocument& document);
Result<void> AttachSkeletonMetadata(AnimationAsset& asset, const ModelAssetData& model);
/** \brief CPU姿勢資源の共有cache。geometry/image/GPUを生成せず、clockを保持しない。 */
class AnimationPoseAssets final {
public:
    void SetRoot(const std::filesystem::path& root);
    void Invalidate();
    Result<std::shared_ptr<const ModelAssetData>> Load(const std::string& clip, const std::string& target, AnimationClipAssets& clips);
    Result<std::vector<std::string>> ModelJoints(const std::string& reference);
private:
    Result<std::shared_ptr<const ModelAssetData>> Model(const std::string& reference);
    std::filesystem::path root_;
    std::map<std::filesystem::path, std::shared_ptr<const ModelAssetData>> models_;
    std::map<std::pair<std::string, std::string>, std::shared_ptr<const ModelAssetData>> poses_;
};
struct RootMotionDelta {
    Vector3 translation;
    Quaternion rotation;
};
struct RootMotionRange {
    std::string asset;
    double begin = 0, end = 0;
    bool loop = true;
    float weight = 1;
    PoseBlendMode mode = PoseBlendMode::Normalized;
    std::vector<std::string> mask;
    uint64_t repeats = 1;
    bool sequential = false; //< Timelineの連続区間はlayer加算とは区別して剛体差分を合成する
};
/** \brief rootの剛体差分。loop/reverseを合成し、SeekでEntityを動かす処理は呼出側が抑止する。 */
Result<RootMotionDelta> ExtractRootMotion(const ModelAssetData& asset, uint32_t root, double begin, double end, bool loop);
RootMotionDelta RepeatRootMotion(const RootMotionDelta& delta, uint64_t count);
RootMotionDelta ComposeRootMotion(const RootMotionDelta& first, const RootMotionDelta& second);
}
