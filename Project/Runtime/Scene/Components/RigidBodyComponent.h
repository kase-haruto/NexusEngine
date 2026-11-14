#pragma once
#include "Foundation/Reflection/ReflectionMarkers.h"
#include <array>
#include <cmath>
NEXUS_REFLECT_COMPONENT(RigidBodyComponent, "RigidBody", "component.rigidBody", enabled, motion, shape, halfExtent, radius, halfHeight, mass, friction, restitution, gravityScale, velocity, angularVelocity, sensor, continuous, collisionLayer, collisionMask)

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * RigidBodyComponent
 * - 既存ECSが所有するPhysicsの保存値。Jolt Body/Shape pointerは保持・保存しない
 * - motion:0=Static,1=Dynamic,2=Kinematic。shape:0=Box,1=Sphere,2=Capsule
 * - velocityはsolver結果を保存し、Scene snapshot/Reloadでも同じWorld状態を復元する
 *---------------------------------------------------------------------------------------*/
struct RigidBodyComponent {
    bool enabled = true; //< 無効時はsolver bodyを解除する
    int motion = 1, shape = 0; //< 公開値enum。Jolt内部enumのABIに依存しない
    std::array<float, 3> halfExtent{0.5f,0.5f,0.5f}; //< Boxの半径。Transform scaleも形状へ反映する
    float radius = 0.5f, halfHeight = 0.5f; //< Sphere/Capsuleの形状値
    float mass = 1, friction = 0.5f, restitution = 0, gravityScale = 1; //< SI単位。Staticでは質量を適用しない
    std::array<float, 3> velocity{}, angularVelocity{}; //< World座標のm/s、rad/s
    bool sensor = false; //< 接触応答のないtrigger body
    bool continuous = false; //< DynamicでLinearCast CCDを使い、高速移動時のすり抜けを抑制する
    int collisionLayer = 0, collisionMask = -1; //< 0～31の所属layerと相手layerの32bit mask。双方が許可した接触だけを作る
    std::array<float, 3> force{}, impulse{}; //< 次Physics stepで消費する値。保存対象外
    /** \brief solverへ非有限値・不正形状を渡さない。Editorの共通setterも同じ検証を使う */
    bool IsValid() const noexcept {
        if(motion < 0 || motion > 2 || shape < 0 || shape > 2 || collisionLayer < 0 || collisionLayer > 31) return false;
        for(float value : halfExtent) if(!std::isfinite(value) || value <= 0 || value > 100000) return false;
        for(const auto* vector : {&velocity,&angularVelocity}) for(float value : *vector) if(!std::isfinite(value) || std::abs(value)>1000000) return false;
        for(const auto* vector : {&force,&impulse}) for(float value : *vector) if(!std::isfinite(value) || std::abs(value)>1000000000) return false;
        return std::isfinite(radius) && radius > 0 && radius <= 100000 && std::isfinite(halfHeight) && halfHeight >= 0 && halfHeight <= 100000 &&
            std::isfinite(mass) && mass >= 0.0001f && mass <= 1000000 && std::isfinite(friction) && friction >= 0 && friction <= 1 &&
            std::isfinite(restitution) && restitution >= 0 && restitution <= 1 && std::isfinite(gravityScale) && gravityScale >= 0 && gravityScale <= 100;
    }
};
}
