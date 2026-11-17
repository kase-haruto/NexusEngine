#include "AnimationUpdateSystem.h"
namespace NexusEngine {
AnimationUpdateSystem::AnimationUpdateSystem(AnimationRuntime& runtime, const ReflectionRegistration& reflection)
    : runtime_(runtime), reflection_(reflection) {
}
SystemPhase AnimationUpdateSystem::GetPhase() const noexcept {
    return SystemPhase::Animation;
}
std::span<const std::type_index> AnimationUpdateSystem::GetReadComponents() const noexcept {
    return {};
}
std::span<const std::type_index> AnimationUpdateSystem::GetWriteComponents() const noexcept {
    return {};
}
bool AnimationUpdateSystem::HasUnspecifiedComponentAccess() const noexcept {
    return true;
}
Result<void> AnimationUpdateSystem::Update(Scene& scene, float deltaTime) {
    return runtime_.Update(scene, deltaTime, reflection_);
}
Result<void> AnimationUpdateSystem::FixedUpdate(Scene&, float) {
    // 可変更新と二重に時刻・イベントを進めない。
    return {};
}
}
