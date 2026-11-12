#include "PhysicsContactCollector.h"
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <algorithm>

namespace NexusEngine {
namespace {
using Contact=PhysicsWorld::Contact;
uint64_t Key(EntityId value) { return (uint64_t(value.generation)<<32)|value.index; }
EntityId EntityKey(uint64_t value) { return {static_cast<uint32_t>(value),static_cast<uint32_t>(value>>32)}; }
auto Pair(const Contact& value) { return std::pair{Key(value.first),Key(value.second)}; }
void SortUnique(std::vector<Contact>& values) {
    std::ranges::sort(values,[](const auto& a,const auto& b) { return Pair(a)<Pair(b); });
    values.erase(std::unique(values.begin(),values.end(),[](const auto& a,const auto& b) { return Pair(a)==Pair(b); }),values.end());
}
bool Contains(const std::vector<Contact>& values,const Contact& value) {
    return std::binary_search(values.begin(),values.end(),value,[](const auto& a,const auto& b) { return Pair(a)<Pair(b); });
}
}
PhysicsContactCollector::PhysicsContactCollector() {
    previous_.reserve(65536); current_.reserve(65536); seen_.reserve(65536); events_.reserve(4096);
}
void PhysicsContactCollector::BeginFrame() noexcept { ++frame_; events_.clear(); seen_.clear(); current_.clear(); dropped_=0; }
void PhysicsContactCollector::Clear() noexcept { cache_.clear(); previous_.clear(); current_.clear(); seen_.clear(); events_.clear(); dropped_=0; }
void PhysicsContactCollector::Record(const JPH::Body& first,const JPH::Body& second,const JPH::ContactManifold& manifold) noexcept {
    try {
        const JPH::SubShapeIDPair key(first.GetID(),manifold.mSubShapeID1,second.GetID(),manifold.mSubShapeID2);
        auto found=cache_.find(key);
        if(found==cache_.end()) {
            if(cache_.size()>=65536) { ++dropped_; return; }
            found=cache_.emplace(key,Cached{}).first;
        }
        auto& cached=found->second; Contact value;
        value.first=EntityKey(first.GetUserData()); value.second=EntityKey(second.GetUserData());
        const auto point=manifold.mRelativeContactPointsOn1.empty() ? manifold.mBaseOffset :
            (manifold.GetWorldSpaceContactPointOn1(0)+manifold.GetWorldSpaceContactPointOn2(0))*0.5f;
        value.point={point.GetX(),point.GetY(),point.GetZ()};
        value.normal={manifold.mWorldSpaceNormal.GetX(),manifold.mWorldSpaceNormal.GetY(),manifold.mWorldSpaceNormal.GetZ()};
        value.sensor=first.IsSensor() || second.IsSensor();
        if(Key(value.second)<Key(value.first)) { std::swap(value.first,value.second); for(auto& axis:value.normal) axis=-axis; }
        cached.contact=value; cached.removed=false;
        if(cached.seenFrame==frame_) seen_[cached.seenIndex]=value;
        else if(seen_.size()<65536) { cached.seenFrame=frame_; cached.seenIndex=seen_.size(); seen_.push_back(value); }
        else ++dropped_;
    } catch(...) { ++dropped_; } // Joltのlockを保持するcallbackから例外を伝搬しない。
}
void PhysicsContactCollector::OnContactAdded(const JPH::Body& a,const JPH::Body& b,const JPH::ContactManifold& manifold,JPH::ContactSettings&) noexcept { Record(a,b,manifold); }
void PhysicsContactCollector::OnContactPersisted(const JPH::Body& a,const JPH::Body& b,const JPH::ContactManifold& manifold,JPH::ContactSettings&) noexcept { Record(a,b,manifold); }
void PhysicsContactCollector::OnContactRemoved(const JPH::SubShapeIDPair& key) noexcept {
    // このcallbackではBodyが既に破棄されている場合があるため、識別子だけを記録する。
    if(auto found=cache_.find(key);found!=cache_.end()) found->second.removed=true;
}
void PhysicsContactCollector::Emit(Contact contact,uint32_t kind) noexcept {
    if(events_.size()==4096) { ++dropped_; return; }
    contact.kind=kind; events_.push_back(contact);
}
void PhysicsContactCollector::FinishFrame(const JPH::BodyLockInterface& bodies) {
    for(auto it=cache_.begin();it!=cache_.end();) {
        if(it->second.removed) {
            bool firstSleeping=false,secondSleeping=false;
            // 同じmutexへ二重にlockしないよう、借用範囲を分ける。job完了後だけ実行する。
            { JPH::BodyLockRead lock(bodies,it->first.GetBody1ID()); firstSleeping=lock.Succeeded() && !lock.GetBody().IsActive(); }
            { JPH::BodyLockRead lock(bodies,it->first.GetBody2ID()); secondSleeping=lock.Succeeded() && !lock.GetBody().IsActive(); }
            if(!firstSleeping || !secondSleeping) { it=cache_.erase(it); continue; }
        }
        current_.push_back(it->second.contact); ++it;
    }
    SortUnique(current_); SortUnique(seen_);
    for(const auto& contact:current_) Emit(contact,Contains(previous_,contact) ? 2u : 1u);
    for(const auto& contact:previous_) if(!Contains(current_,contact)) Emit(contact,3);
    // 複数substepの途中だけで発生した接触も、ゲームへEnterとExitの両方を渡す。
    for(const auto& contact:seen_) if(!Contains(previous_,contact) && !Contains(current_,contact)) { Emit(contact,1); Emit(contact,3); }
    previous_.swap(current_);
}
}
