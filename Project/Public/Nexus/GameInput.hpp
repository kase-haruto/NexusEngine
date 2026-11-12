#pragma once
#include "GameInput.h"
namespace NexusGame {
/*-----------------------------------------------------------------------------------------
 * Input
 * - 既存servicesからframe入力を一括取得するC++ Facade
 * - WindowやEditorを参照せず、Module寿命の借用clientだけを保持する
 *---------------------------------------------------------------------------------------*/
class Input final {
public:
    /** \brief Loaderが所有するservices接続を借用する */
    explicit Input(const NexusGameServicesContextV1& context) noexcept : api_(context.api),
        client_(context.abiVersion==1 && context.structSize==sizeof(context) ? context.client : 0) {}
    /** \brief frame全体を値コピーする。高頻度処理では一回取得した値を再利用する */
    NexusGameStatus Read(NexusInputFrameV1& output) const noexcept {
        output={};
        if(api_.abiVersion!=1 || api_.structSize!=sizeof(api_) || !api_.invoke || !client_) return NEXUS_GAME_INVALID_ARGUMENT;
        NexusInputFrameV1 next{}; uint32_t length=0;
        const auto status=api_.invoke(client_,{NEXUS_INPUT_COMMAND,sizeof(NEXUS_INPUT_COMMAND)-1},1,{}, {},
            reinterpret_cast<uint8_t*>(&next),sizeof(next),&length);
        if(status!=NEXUS_GAME_OK) return status;
        if(length!=sizeof(next) || next.focused>1 || next.reserved[0] || next.reserved[1]) return NEXUS_GAME_FAILED;
        output=next; return NEXUS_GAME_OK;
    }
    /** \brief 4台分を一括取得する。slotはXInput user indexで、切断後の再利用に注意する */
    NexusGameStatus ReadGamepads(NexusGamepadsFrameV1& output) const noexcept {
        output={};
        if(api_.abiVersion!=1 || api_.structSize!=sizeof(api_) || !api_.invoke || !client_) return NEXUS_GAME_INVALID_ARGUMENT;
        const NexusGamepadRequestV1 request{1}; NexusGamepadsFrameV1 next{}; uint32_t length=0;
        const auto status=api_.invoke(client_,{NEXUS_INPUT_COMMAND,sizeof(NEXUS_INPUT_COMMAND)-1},NEXUS_INPUT_PROTOCOL_VERSION,{},
            {reinterpret_cast<const uint8_t*>(&request),sizeof(request)},reinterpret_cast<uint8_t*>(&next),sizeof(next),&length);
        if(status!=NEXUS_GAME_OK) return status;
        if(length!=sizeof(next) || next.count!=NEXUS_GAMEPAD_COUNT || next.focused>1 || next.reserved[0] || next.reserved[1]) return NEXUS_GAME_FAILED;
        output=next; return NEXUS_GAME_OK;
    }
private:
    NexusGameServicesApiV1 api_{}; //< Engine常駐の関数表コピー
    uint64_t client_=0; //< Module所有者が終了すると失効する借用接続
};
}
