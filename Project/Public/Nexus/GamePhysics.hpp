#pragma once
#include "GamePhysics.h"
#include <array>
#include <cstring>

namespace NexusGame {
/*-----------------------------------------------------------------------------------------
 * Physics
 * - Module寿命の既存services clientを借用するC++用の型付きPhysics Facade
 * - 呼出しはWorldの所有threadのみ。BodyやEntityの内部pointerを保存しない
 *---------------------------------------------------------------------------------------*/
class Physics final {
public:
    /** \brief Loaderが所有するservices接続を借用する。Module終了後はEngineが呼出しを拒否する */
    explicit Physics(const NexusGameServicesContextV1& context) noexcept : api_(context.api),
        client_(context.abiVersion==1 && context.structSize==sizeof(context) ? context.client : 0) {}
    /** \brief 最近傍のhitをコピーする。成功かつoutput.entity.world==0なら無衝突 */
    NexusGameStatus Raycast(const std::array<float,3>& origin, const std::array<float,3>& direction,
        float distance, NexusPhysicsHitV1& output) const noexcept {
        output={};
        const NexusPhysicsRequestV1 input{1,0,{origin[0],origin[1],origin[2]},
            {direction[0],direction[1],direction[2]},distance,0};
        NexusPhysicsHitV1 result{}; uint32_t count=0;
        const auto status=Invoke({},input,reinterpret_cast<uint8_t*>(&result),sizeof(result),count);
        if(status!=NEXUS_GAME_OK) return status;
        if(count!=sizeof(result)) return NEXUS_GAME_FAILED;
        output=result;
        return NEXUS_GAME_OK;
    }
    /** \brief 次のPhysics stepへWorld座標の力（N）を蓄積する。Dynamic bodyを要求する */
    NexusGameStatus AddForce(NexusEntityHandle entity, const std::array<float,3>& force) const noexcept {
        return Push(2,entity,force);
    }
    /** \brief 次のPhysics stepへWorld座標の力積（N s）を蓄積する */
    NexusGameStatus AddImpulse(NexusEntityHandle entity, const std::array<float,3>& impulse) const noexcept {
        return Push(3,entity,impulse);
    }
    /** \brief 既存Event queueへの接触通知をこのclientだけで購読/解除する */
    NexusGameStatus EnableContactEvents(bool enabled=true) const noexcept {
        const NexusPhysicsRequestV1 input{5,enabled ? 1u : 0u,{},{},0,0}; uint32_t length=0;
        return Invoke({},input,nullptr,0,length);
    }
    /** \brief 非消費snapshotの一pageをコピーする。header.nextがUINT32_MAXで終端 */
    NexusGameStatus ReadContacts(uint32_t offset,NexusPhysicsContactV1* output,uint32_t capacity,NexusPhysicsContactsHeaderV1& header) const noexcept {
        header={};
        if(capacity && !output) return NEXUS_GAME_INVALID_ARGUMENT;
        const NexusPhysicsRequestV1 input{4,offset,{},{},0,0}; std::array<uint8_t,4096> response{}; uint32_t length=0;
        const auto status=Invoke({},input,response.data(),static_cast<uint32_t>(response.size()),length);
        if(status!=NEXUS_GAME_OK) return status;
        if(length<sizeof(header)) return NEXUS_GAME_FAILED;
        std::memcpy(&header,response.data(),sizeof(header));
        if(header.reserved || header.count>NEXUS_PHYSICS_CONTACT_PAGE_CAPACITY || length!=sizeof(header)+size_t(header.count)*sizeof(NexusPhysicsContactV1)) return NEXUS_GAME_FAILED;
        if(header.count>capacity) return NEXUS_GAME_BUFFER_TOO_SMALL;
        if(header.count) std::memcpy(output,response.data()+sizeof(header),header.count*sizeof(NexusPhysicsContactV1));
        return NEXUS_GAME_OK;
    }
private:
    /** \brief 値だけの要求を送信する。借用bufferは同期呼出しの終了まで有効 */
    NexusGameStatus Invoke(NexusEntityHandle target,const NexusPhysicsRequestV1& input,
        uint8_t* output,uint32_t capacity,uint32_t& count) const noexcept {
        if(api_.abiVersion!=1 || api_.structSize!=sizeof(api_) || !api_.invoke || !client_) return NEXUS_GAME_INVALID_ARGUMENT;
        return api_.invoke(client_,{NEXUS_PHYSICS_COMMAND,sizeof(NEXUS_PHYSICS_COMMAND)-1},NEXUS_PHYSICS_PROTOCOL_VERSION,target,
            {reinterpret_cast<const uint8_t*>(&input),sizeof(input)},output,capacity,&count);
    }
    /** \brief Force/Impulseは応答値を持たず、全成分の検証後にだけ蓄積する */
    NexusGameStatus Push(uint32_t operation,NexusEntityHandle entity,const std::array<float,3>& value) const noexcept {
        const NexusPhysicsRequestV1 input{operation,0,{value[0],value[1],value[2]},{},0,0};
        uint32_t count=0;
        const auto status=Invoke(entity,input,nullptr,0,count);
        return status==NEXUS_GAME_OK && count ? NEXUS_GAME_FAILED : status;
    }
    NexusGameServicesApiV1 api_{}; //< Engine常駐表の値コピー。Jolt/DLL内関数を直接保持しない
    uint64_t client_=0; //< 借用capability。所有者の終了後は無効
};
}
