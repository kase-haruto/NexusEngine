#pragma once
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include "PhysicsWorld.h"
#include <map>
#include <vector>
namespace JPH { class BodyLockInterface; }
namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * PhysicsContactCollector
 * - Jolt callbackの値だけを採取し、solver更新後にEntity pairのEnter/Stay/Exitへ集約する
 * - callback中にはゲーム/Managed/locking BodyInterfaceを呼ばず、例外を外へ出さない
 * - sleepingによるJolt内部contact解除を、実際の衝突終了と混同しない
 *---------------------------------------------------------------------------------------*/
class PhysicsContactCollector final : public JPH::ContactListener {
public:
    PhysicsContactCollector();
    /** \brief step開始時に通知bufferだけを消す。継続接触の識別cacheは維持する */
    void BeginFrame() noexcept;
    /** \brief 全Jolt job終了後にのみ呼ぶ。無効Bodyをdereferenceせずsleep状態を検査する */
    void FinishFrame(const JPH::BodyLockInterface& bodies);
    /** \brief Scene交換時に旧Entityの通知と接触cacheを一緒に破棄する */
    void Clear() noexcept;
    void OnContactAdded(const JPH::Body&,const JPH::Body&,const JPH::ContactManifold&,JPH::ContactSettings&) noexcept override;
    void OnContactPersisted(const JPH::Body&,const JPH::Body&,const JPH::ContactManifold&,JPH::ContactSettings&) noexcept override;
    void OnContactRemoved(const JPH::SubShapeIDPair&) noexcept override;
    std::span<const PhysicsWorld::Contact> Events() const noexcept { return events_; }
    uint32_t Dropped() const noexcept { return dropped_; }
private:
    struct Cached {
        PhysicsWorld::Contact contact; //< Body pointerではなくEntity世代と値だけを保存する
        bool removed=false; //< callback中にはBodyを照会できないため、step終了後に確定する
        uint64_t seenFrame=0; size_t seenIndex=0; //< 同じsubshapeの複数substep通知を一つの観測値へまとめる
    };
    void Record(const JPH::Body&,const JPH::Body&,const JPH::ContactManifold&) noexcept;
    void Emit(PhysicsWorld::Contact contact,uint32_t kind) noexcept;
    std::map<JPH::SubShapeIDPair,Cached> cache_; //< 最大65536 subshape接触。別ECSのComponentを所有しない
    std::vector<PhysicsWorld::Contact> previous_,current_,seen_,events_; //< 予約済みの値buffer。通知上限は4096
    uint64_t frame_=0; //< 連続substepでの重複観測を区別する内部世代
    uint32_t dropped_=0; //< 上限/確保失敗による欠落を公開snapshotへ明示する
};
}
