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
};
}
