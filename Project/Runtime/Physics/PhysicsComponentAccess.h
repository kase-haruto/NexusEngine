#pragma once
#include "Runtime/Scene/Scene.h"
#include "Runtime/Scene/Components/ColliderComponent.h"
namespace NexusEngine {
/**
 * \brief 同じ既存EntityのRigidBody/Colliderから一つの計算用settingsを解決する
 * \return 無効Component・未配置ならfalse。falseと値検証失敗は呼出側で区別する
 * \note Collider単体はStatic、RigidBody単体は従来の内蔵形状を利用する。pointerは保持しない
 */
inline bool ResolvePhysicsBody(Entity entity,RigidBodyComponent& value) noexcept {
    const auto* rigid=entity.GetComponent<RigidBodyComponent>();
    const auto* collider=entity.GetComponent<ColliderComponent>();
    if((!rigid && !collider) || (rigid && !rigid->enabled) || (collider && !collider->enabled)) return false;
    value=rigid ? *rigid : RigidBodyComponent{};
    if(!rigid) value.motion=0;
    if(collider) collider->ApplyTo(value);
    return true;
}
}
