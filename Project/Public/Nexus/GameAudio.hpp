#pragma once
#include "GameAudio.h"
#include <array>
#include <cstring>
namespace NexusGame {
/*-----------------------------------------------------------------------------------------
 * Audio
 * - Native Moduleのservices clientへ帰属する音声操作Facade
 * - Handleの所有者はclientであり、Module解除時にはEngineが全voiceを停止する
 *---------------------------------------------------------------------------------------*/
class Audio final {
public:
    /** \brief Loader所有の接続を借用する。出力deviceやsample bufferはEngine所有 */
    explicit Audio(const NexusGameServicesContextV1& context) noexcept : api_(context.api),
        client_(context.abiVersion==1 && context.structSize==sizeof(context) ? context.client : 0) {}
    /** \brief Project相対WAVを再生する。文字列は同期呼出し中だけ借用する */
    NexusGameStatus Play(NexusGameString path,uint64_t& voice,float volume=1,bool loop=false) const noexcept {
        voice=0;
        if(!path.data || !path.length || path.length>2048) return NEXUS_GAME_INVALID_ARGUMENT;
        return Invoke({1,path.length,0,volume,loop ? 1u : 0u},path,voice);
    }
    /** \brief 所有voiceを停止・破棄する */
    NexusGameStatus Stop(uint64_t voice) const noexcept { uint64_t unused=0; return Invoke({2,0,voice,0,0},{},unused); }
    /** \brief 再生bufferが残っているかを照会する */
    NexusGameStatus IsPlaying(uint64_t voice,bool& playing) const noexcept {
        playing=false; uint64_t value=0;
        const auto status=Invoke({3,0,voice,0,0},{},value);
        if(status!=NEXUS_GAME_OK) return status;
        if(value>1) return NEXUS_GAME_FAILED;
        playing=value!=0; return NEXUS_GAME_OK;
    }
    /** \brief 音量0〜4を設定する */
    NexusGameStatus SetVolume(uint64_t voice,float volume) const noexcept { uint64_t unused=0; return Invoke({4,0,voice,volume,0},{},unused); }
private:
    /** \brief ABI bufferを組み立て、例外・STLを境界へ渡さず既存servicesを呼ぶ */
    NexusGameStatus Invoke(const NexusAudioRequestV1& header,NexusGameString path,uint64_t& output) const noexcept {
        if(api_.abiVersion!=1 || api_.structSize!=sizeof(api_) || !api_.invoke || !client_) return NEXUS_GAME_INVALID_ARGUMENT;
        std::array<uint8_t,24+2048> input{};
        std::memcpy(input.data(),&header,sizeof(header));
        if(path.length) std::memcpy(input.data()+sizeof(header),path.data,path.length);
        uint64_t result=0; uint32_t count=0;
        const auto status=api_.invoke(client_,{NEXUS_AUDIO_COMMAND,sizeof(NEXUS_AUDIO_COMMAND)-1},1,{},
            {input.data(),static_cast<uint32_t>(sizeof(header)+path.length)},reinterpret_cast<uint8_t*>(&result),sizeof(result),&count);
        if(status!=NEXUS_GAME_OK) return status;
        if(count!=((header.operation==1 || header.operation==3) ? sizeof(uint64_t) : 0)) return NEXUS_GAME_FAILED;
        output=result; return NEXUS_GAME_OK;
    }
    NexusGameServicesApiV1 api_{}; //< Engine常駐表のコピー
    uint64_t client_=0; //< 音声所有者となるModule寿命の借用token
};
}
