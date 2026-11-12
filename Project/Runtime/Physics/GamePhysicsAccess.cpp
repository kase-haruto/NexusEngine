#include "GamePhysicsAccess.h"
#include "Runtime/Level/Level.h"
#include "Runtime/Scene/Components/RigidBodyComponent.h"
#include "Runtime/Scene/Components/ColliderComponent.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace NexusEngine {
NexusGameStatus CopyPhysicsContacts(Level& level,uint64_t world,uint32_t offset,uint8_t* response,uint32_t capacity,uint32_t* length) noexcept {
    if(!length || (capacity && !response)) return NEXUS_GAME_INVALID_ARGUMENT;
    *length=0;
    const auto contacts=level.PhysicsContacts();
    if(offset>contacts.size()) return NEXUS_GAME_INVALID_ARGUMENT;
    const auto count=static_cast<uint32_t>((std::min)(size_t(NEXUS_PHYSICS_CONTACT_PAGE_CAPACITY),contacts.size()-offset));
    const NexusPhysicsContactsHeaderV1 header{count,offset+count<contacts.size() ? offset+count : UINT32_MAX,level.DroppedPhysicsContacts(),0};
    *length=static_cast<uint32_t>(sizeof(header)+size_t(count)*sizeof(NexusPhysicsContactV1));
    if(capacity<*length) return NEXUS_GAME_BUFFER_TOO_SMALL;
    std::memcpy(response,&header,sizeof(header));
    for(uint32_t index=0;index<count;++index) {
        const auto& value=contacts[offset+index]; NexusPhysicsContactV1 contact{};
        contact.first={world,value.first.index,value.first.generation}; contact.second={world,value.second.index,value.second.generation};
        for(size_t axis=0;axis<3;++axis) { contact.point[axis]=value.point[axis]; contact.normal[axis]=value.normal[axis]; }
        contact.kind=value.kind; contact.sensor=value.sensor ? 1u : 0u;
        std::memcpy(response+sizeof(header)+index*sizeof(contact),&contact,sizeof(contact));
    }
    return NEXUS_GAME_OK;
}
NexusGameStatus InvokeGamePhysics(Level& level, uint64_t world, NexusEntityHandle target,
    NexusGameBytes request, uint8_t* response, uint32_t capacity, uint32_t* length) noexcept {
    if(!length) return NEXUS_GAME_INVALID_ARGUMENT;
    *length=0;
    if(request.length!=sizeof(NexusPhysicsRequestV1) || !request.data || (capacity && !response)) return NEXUS_GAME_INVALID_ARGUMENT;
    NexusPhysicsRequestV1 input{};
    std::memcpy(&input,request.data,sizeof(input));
    if(input.operation<1 || input.operation>4 || (input.operation!=4 && input.reserved) || input.reserved2) return NEXUS_GAME_INVALID_ARGUMENT;
    for(float item:input.vector) if(!std::isfinite(item)) return NEXUS_GAME_INVALID_ARGUMENT;
    for(float item:input.direction) if(!std::isfinite(item)) return NEXUS_GAME_INVALID_ARGUMENT;
    if(!std::isfinite(input.distance)) return NEXUS_GAME_INVALID_ARGUMENT;
    try {
        if(!level.GetScene()) return NEXUS_GAME_INVALID_HANDLE;
        if(input.operation==4) {
            if(target.world || target.index || target.generation || input.distance!=0) return NEXUS_GAME_INVALID_ARGUMENT;
            for(float item:input.vector) if(item!=0) return NEXUS_GAME_INVALID_ARGUMENT;
            for(float item:input.direction) if(item!=0) return NEXUS_GAME_INVALID_ARGUMENT;
            return CopyPhysicsContacts(level,world,input.reserved,response,capacity,length);
        }
        if(input.operation==1) {
            // 先に容量を確定し、retryでqueryを二度実行しなくてもよい契約にする。
            if(target.world || target.index || target.generation || input.distance<=0 || input.distance>1000000) return NEXUS_GAME_INVALID_ARGUMENT;
            const double lengthSquared=static_cast<double>(input.direction[0])*input.direction[0]+
                static_cast<double>(input.direction[1])*input.direction[1]+static_cast<double>(input.direction[2])*input.direction[2];
            if(lengthSquared<1e-12 || lengthSquared>1e30) return NEXUS_GAME_INVALID_ARGUMENT;
            *length=sizeof(NexusPhysicsHitV1);
            if(capacity<*length) return NEXUS_GAME_BUFFER_TOO_SMALL;
            auto result=level.Raycast({input.vector[0],input.vector[1],input.vector[2]},
                {input.direction[0],input.direction[1],input.direction[2]},input.distance);
            if(!result) { *length=0; return NEXUS_GAME_FAILED; }
            NexusPhysicsHitV1 hit{};
            if(result->entity.IsValid()) {
                hit.entity={world,result->entity.index,result->entity.generation};
                for(size_t i=0;i<3;++i) { hit.point[i]=result->point[i]; hit.normal[i]=result->normal[i]; }
                hit.distance=result->distance;
            }
            std::memcpy(response,&hit,sizeof(hit));
            return NEXUS_GAME_OK;
        }
        if(input.distance!=0 || input.direction[0]!=0 || input.direction[1]!=0 || input.direction[2]!=0) return NEXUS_GAME_INVALID_ARGUMENT;
        // Bodyの内部pointerをcacheせず、呼出しごとに既存Entity世代とComponentを解決する。
        if(target.world!=world) return NEXUS_GAME_INVALID_HANDLE;
        auto entity=level.GetScene()->GetEntity({target.index,target.generation});
        if(!entity.IsAlive()) return NEXUS_GAME_INVALID_HANDLE;
        auto* body=entity.GetComponent<RigidBodyComponent>();
        if(const auto* collider=entity.GetComponent<ColliderComponent>(); collider && !collider->enabled) return NEXUS_GAME_INVALID_ARGUMENT;
        if(!body || !body->enabled || body->motion!=1 || !body->IsValid()) return NEXUS_GAME_INVALID_ARGUMENT;
        auto& destination=input.operation==2 ? body->force : body->impulse;
        std::array<float,3> next{};
        for(size_t i=0;i<3;++i) { next[i]=destination[i]+input.vector[i]; if(!std::isfinite(next[i]) || std::abs(next[i])>1000000000) return NEXUS_GAME_INVALID_ARGUMENT; }
        // 力はECS値として蓄積するため、Managed reloadのScene snapshotで復元できる。
        destination=next;
        return NEXUS_GAME_OK;
    } catch(...) { *length=0; return NEXUS_GAME_FAILED; }
}
}
