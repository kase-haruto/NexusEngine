#include "PhysicsUpdateSystem.h"
#include "PhysicsComponentAccess.h"
#include "Runtime/Scene/Scene.h"
#include "Runtime/Scene/Components/HierarchyComponent.h"
#include "Runtime/Scene/Components/RigidBodyComponent.h"
#include <cmath>

namespace NexusEngine {
std::span<const std::type_index> PhysicsUpdateSystem::GetReadComponents() const noexcept {
    static const std::array types{std::type_index(typeid(HierarchyComponent)),std::type_index(typeid(ColliderComponent))};
    return types;
}
std::span<const std::type_index> PhysicsUpdateSystem::GetWriteComponents() const noexcept {
    static const std::array types{std::type_index(typeid(TransformComponent)), std::type_index(typeid(RigidBodyComponent))};
    return types;
}
Result<void> PhysicsUpdateSystem::Update(Scene& scene, float deltaTime) {
    if(!std::isfinite(deltaTime) || deltaTime < 0)
        return std::unexpected(Error(ErrorCategory::Scene, 260, "Physics delta time must be finite and non-negative."));
    // 編集WorldのMatrix更新はUpdate(0)を使用する。物理の検証・生成もPlayまで遅延する。
    if(deltaTime == 0) return {};
    bool required = false;
    scene.ForEachEntity([&](Entity entity) { RigidBodyComponent body; required |= ResolvePhysicsBody(entity,body); });
    if(!required && !world_) return {};
    if(!world_) world_ = std::make_unique<PhysicsWorld>(models_);
    return world_->Step(scene, deltaTime);
}
}
