#pragma once
#include "AnimationClipAssets.h"
#include "AnimationPropertyEvaluator.h"
#include "Runtime/Curves/CurveAssets.h"
#include "AnimationLayers.h"

namespace NexusEngine {
using AnimationRangeSink = std::function<void(Entity, const std::string&, const AnimationClipDocument&, double, double, bool)>;
/** \brief 新しい独立componentを評価する。アセット・binding・clockの所有権を引き取らない。 */
void EvaluateAnimationComponents(Scene& scene, float deltaTime, AnimationClipAssets& clips,
    CurveAssets& curves, AnimationLayerAssets& layers, AnimationPropertyEvaluator& properties, const AnimationRangeSink& events = {});
}
