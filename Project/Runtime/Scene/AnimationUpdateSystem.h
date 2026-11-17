#pragma once
#include "SystemScheduler.h"
#include "Runtime/Animation/AnimationRuntime.h"
namespace NexusEngine {
/** 汎用アニメーションをゲームロジック後、Physics/Transform確定前のAnimation phaseで評価する。 */
class AnimationUpdateSystem final : public ISceneSystem {
public:
    AnimationUpdateSystem(AnimationRuntime& runtime, const ReflectionRegistration& reflection);
    SystemPhase GetPhase() const noexcept override;
    std::span<const std::type_index> GetReadComponents() const noexcept override;
    std::span<const std::type_index> GetWriteComponents() const noexcept override;
    bool HasUnspecifiedComponentAccess() const noexcept override;
    Result<void> Update(Scene& scene, float deltaTime) override;
    Result<void> FixedUpdate(Scene& scene, float deltaTime) override;
private:
    AnimationRuntime& runtime_;
    const ReflectionRegistration& reflection_;
};
}
