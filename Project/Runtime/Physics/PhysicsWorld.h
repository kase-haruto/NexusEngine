#pragma once
#include "Foundation/Error/Result.h"
#include "Runtime/Scene/EntityId.h"
#include <array>
#include <memory>
#include <functional>
#include <string_view>
#include <span>

namespace NexusEngine {
class Scene;
struct ModelAssetData;
/*-----------------------------------------------------------------------------------------
 * PhysicsWorld
 * - 既存SceneのRigidBody値とJolt solverを同期する。Entity/ComponentはSceneが所有する
 * - 内部body IDはowner thread内だけで使用し、外部へはEntityIdとコピー値を返す
 * - Scene交換時はECS値からbodyを再生成し、旧Sceneのobject/pointerを使わない
 *---------------------------------------------------------------------------------------*/
class PhysicsWorld final {
public:
    using ModelSource=std::function<Result<const ModelAssetData*>(std::string_view)>; //< Hostの既存CPU asset cacheを同期借用する
    struct Hit { EntityId entity; std::array<float,3> point{}, normal{}; float distance=0; }; //< 外部へ返す値コピー
    /** Entity pair単位の接触値。kindは1=Enter、2=Stay、3=Exit。ExitではEntityが破棄済みの場合もある。 */
    struct Contact {
        EntityId first,second; //< 世代を保存し、slot再利用を別Entityとして扱う
        std::array<float,3> point{},normal{}; //< normalはfirstからsecondを離す向き
        uint32_t kind=0; //< 1frame内に接触・分離した場合はEnter/Exitの両方を返す
        bool sensor=false; //< どちらかのColliderがsensorの場合にtrue
    };
    /** \brief Process共通のJolt登録を確立し、このWorld専用のsolverを生成する */
    explicit PhysicsWorld(ModelSource models = {});
    /** \brief bodyをsolverから解除・破棄した後、solverと作業領域を解放する */
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&)=delete;
    PhysicsWorld& operator=(const PhysicsWorld&)=delete;
    /** \brief game logicの後、Transform確定の前に同期・simulation・結果書戻しを行う */
    Result<void> Step(Scene& scene, float deltaTime);
    /** \brief 現在のSceneを同期し、正規化した方向へ最近傍raycastを行う。no-hitはentity無効 */
    Result<Hit> Raycast(Scene& scene, const std::array<float,3>& origin, const std::array<float,3>& direction, float distance, uint32_t layerMask = UINT32_MAX);
    /** \brief 最後のPhysics stepの非消費snapshot。借用spanは次step/World破棄まで有効 */
    std::span<const Contact> Contacts() const noexcept;
    uint32_t DroppedContacts() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_; //< Jolt依存・body所有は実装へ限定し、SDKへ第三者ABIを漏らさない
};
}
