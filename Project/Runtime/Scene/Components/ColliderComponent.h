#pragma once
#include "RigidBodyComponent.h"
#include <string>
NEXUS_REFLECT_COMPONENT(ColliderComponent, "Collider", "component.collider", enabled, meshAsset, shape, halfExtent, radius, halfHeight, friction, restitution, sensor, collisionLayer, collisionMask)
namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * ColliderComponent
 * - 既存ECS内で衝突形状と材質を所有する。単独配置時はStatic colliderになる
 * - RigidBodyと同居する場合は形状・材質だけを上書きし、速度や質量の所有を奪わない
 * - Jolt Shape pointerは保存しない。旧RigidBodyの形状fieldは既存データのfallbackとして残す
 *---------------------------------------------------------------------------------------*/
struct ColliderComponent {
    bool enabled=true; //< falseなら同じEntityのphysics bodyを解除する
    std::string meshAsset; //< 既存ModelAssetLibraryの参照。空のMesh/Convexは編集中の未設定状態
    int shape=0; //< 0=Box、1=Sphere、2=Capsule、3=Triangle Mesh、4=Convex Hull
    std::array<float,3> halfExtent{0.5f,0.5f,0.5f}; //< scale適用前のBox半寸法
    float radius=0.5f,halfHeight=0.5f; //< Sphere/Capsuleの形状寸法
    float friction=0.5f,restitution=0; //< 接触材質。RigidBodyの運動パラメータと分離する
    bool sensor=false; //< 物理接触応答を抑制する
    int collisionLayer = 0, collisionMask = -1; //< 所属layer。maskは旧保存データ互換用で、接触判定には使わない
    /** \brief backend非依存の計算用settingsへ形状・材質を反映する */
    void ApplyTo(RigidBodyComponent& body) const noexcept {
        body.shape=shape; body.halfExtent=halfExtent; body.radius=radius; body.halfHeight=halfHeight;
        body.friction=friction; body.restitution=restitution; body.sensor=sensor;
        body.collisionLayer = collisionLayer;
        body.collisionMask = collisionMask;
    }
    /** \brief 保存・編集可能な形状値を検証する。Meshの設定完了はsimulation開始時に別途検査する */
    bool IsValid() const noexcept {
        // 形状を先に選び、後からAssetを割り当てる編集を許可する。実行時の未設定検出はPhysicsWorldが行う。
        if(shape < 0 || shape > 4 || meshAsset.size() > 4096) {
            return false;
        }
        for(unsigned char value:meshAsset) if(value<32) return false;
        RigidBodyComponent value; ApplyTo(value); if(shape>=3) value.shape=0;
        return value.IsValid();
    }
};
}
