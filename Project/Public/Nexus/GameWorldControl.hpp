#pragma once
#include "GameWorldControl.h"
#include <array>
#include <cstring>
namespace NexusGame {
/*-----------------------------------------------------------------------------------------
 * WorldControl
 * - Project内の資源照会と、Runtimeが所有するLevel遷移への要求を公開する
 * - Level/Assetを所有せず、既存servicesのWorld/thread/寿命検査を利用する
 *---------------------------------------------------------------------------------------*/
class WorldControl final {
public:
    /** \brief Module所有のservices接続を借用する */
    explicit WorldControl(const NexusGameServicesContextV1& context) noexcept : api_(context.api),
        client_(context.abiVersion==1 && context.structSize==sizeof(context) ? context.client : 0) {}
    /** \brief 次frame境界でのLevel遷移を要求する。戻り値は要求受理を表し、読込完了ではない */
    NexusGameStatus RequestLevel(NexusGameString path) const noexcept { uint32_t unused=0; return Invoke(1,path,unused); }
    /** \brief Project Root内のfile参照が存在するか確認する。外部への逸脱は失敗する */
    NexusGameStatus AssetExists(NexusGameString path,bool& exists) const noexcept {
        exists=false; uint32_t value=0; const auto result=Invoke(2,path,value);
        if(result!=NEXUS_GAME_OK) return result;
        if(value>1) return NEXUS_GAME_FAILED;
        exists=value!=0; return NEXUS_GAME_OK;
    }
private:
    /** \brief UTF-8参照を同期借用bufferへコピーして予約commandを呼ぶ */
    NexusGameStatus Invoke(uint32_t operation,NexusGameString path,uint32_t& value) const noexcept {
        if(!path.data || !path.length || path.length>2048 || !client_ || api_.abiVersion!=1 || api_.structSize!=sizeof(api_) || !api_.invoke) return NEXUS_GAME_INVALID_ARGUMENT;
        std::array<uint8_t,8+2048> request{};
        const NexusWorldRequestV1 header{operation,path.length}; std::memcpy(request.data(),&header,sizeof(header));
        std::memcpy(request.data()+sizeof(header),path.data,path.length);
        uint32_t result=0,count=0;
        const auto status=api_.invoke(client_,{NEXUS_WORLD_CONTROL_COMMAND,sizeof(NEXUS_WORLD_CONTROL_COMMAND)-1},1,{},
            {request.data(),static_cast<uint32_t>(sizeof(header)+path.length)},reinterpret_cast<uint8_t*>(&result),sizeof(result),&count);
        if(status!=NEXUS_GAME_OK) return status;
        if(count!=(operation==2 ? sizeof(uint32_t) : 0)) return NEXUS_GAME_FAILED;
        value=result; return NEXUS_GAME_OK;
    }
    NexusGameServicesApiV1 api_{}; //< 常駐Engine関数表の値コピー
    uint64_t client_=0; //< Module寿命の借用接続
};
}
