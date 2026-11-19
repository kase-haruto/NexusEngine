#pragma once
#include "ModelAnimation.h"
#include <string>
#include <vector>

namespace NexusEngine {
/** モデルの描画資源から独立した不変クリップ。nodePathsはスケルトンへのバインドに使う。 */
struct AnimationAsset {
    std::string reference;
    std::string source;
    AnimationClip clip;
    std::vector<std::string> nodePaths;
    std::vector<ModelNodeTransform> sourceRestPose; //< glTF由来の互換検査用。 authored legacyは未保持
    std::vector<ModelSkin> sourceSkins; //< bind matrixの検査用。GPU資源を含まない
    std::string skeletonSignature, retargetProfile; //< rest/bind変更の検出と明示的な補正asset
};
}
