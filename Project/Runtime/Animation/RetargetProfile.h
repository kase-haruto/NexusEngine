#pragma once
#include "Foundation/Error/Result.h"
#include "Graphics/Model/AnimationAsset.h"
#include "Graphics/Model/ModelAssetData.h"
#include <filesystem>

namespace NexusEngine {
struct RetargetJointMapping {
    std::string source, target; //< 長さ付き階層path。表示名だけで自動適用しない
    Quaternion axis; //< source local deltaをtarget local座標へ移す基底回転
    Vector3 translationScale{1, 1, 1}; //< 体格差の補正。rest位置自身はtargetの値を使う
    bool operator==(const RetargetJointMapping&) const;
};
/** \brief clipから独立した骨格対応asset。Entityの時計やGPU資源を含まない。 */
struct RetargetProfile {
    std::string sourceModel, targetModel;
    std::string sourceSignature, targetSignature;
    std::vector<RetargetJointMapping> joints;
    bool operator==(const RetargetProfile&) const;
};
std::vector<std::string> SkeletonNodePaths(const ModelAssetData& model);
std::string DisplaySkeletonPath(const std::string& path);
std::string SkeletonSignature(const ModelAssetData& model);
Result<void> ValidateRetargetProfile(const RetargetProfile& profile);
Result<RetargetProfile> LoadRetargetProfile(const std::filesystem::path& file);
Result<void> SaveRetargetProfile(const std::filesystem::path& file, const RetargetProfile& profile);
/** \brief 一意な同名jointの対応候補を生成する。異名/曖昧な対応はEditorで明示する。 */
RetargetProfile SuggestRetargetProfile(const ModelAssetData& source, const ModelAssetData& target);
/** \brief rest相対のlocal TRSを補正しtarget nodeへ再構築する。失敗時は元clipを変更しない。 */
Result<AnimationClip> RetargetAnimation(const AnimationAsset& source, const ModelAssetData& target,
    const RetargetProfile& profile);
/** \brief 補正を指定しない場合の厳密な階層/rest/bind検査とtarget indexへのbinding。 */
Result<AnimationClip> BindCompatibleAnimation(const AnimationAsset& source, const ModelAssetData& target);
}
