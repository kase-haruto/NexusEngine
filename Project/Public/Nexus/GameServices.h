#pragma once
#include "GameApi.h"
#pragma pack(push, 8)
/*-----------------------------------------------------------------------------------------
 * NexusGameBytes / NexusGameEventV1
 * - 言語間で値だけをコピーする範囲とEvent header。ポインタは呼出し中だけ借用する
 * - Event bufferはUTF-8 Type ID、その後にpayloadを連結する。Entityは既存世代付きHandle
 *---------------------------------------------------------------------------------------*/
typedef struct NexusGameBytes { const uint8_t* data; uint32_t length; } NexusGameBytes;
typedef struct NexusGameEventV1 {
    uint64_t sequence;
    NexusEntityHandle source;
    uint32_t version, typeLength, payloadLength, reserved;
} NexusGameEventV1;
/* callback / stateは登録したNative Moduleが所有する。Engineはclient失効前だけ借用する。
 * 例外・STL・allocator所有権を越境させず、応答は4096 byte以下。借用bufferを保持しない。 */
typedef NexusGameStatus (NEXUS_GAME_CALL *NexusGameCommandFn)(void* state, NexusEntityHandle target, NexusGameBytes request,
    uint8_t* response, uint32_t capacity, uint32_t* length);
/*-----------------------------------------------------------------------------------------
 * NexusGameServicesApiV1
 * - 共通Worldの明示的なPublic API登録・値呼出し・有界Event queueを提供する
 * - Managed clientには登録権限を与えない。Gameの内部型・delegateを境界へ公開しない
 *---------------------------------------------------------------------------------------*/
typedef struct NexusGameServicesApiV1 {
    uint32_t abiVersion, structSize;
    NexusGameStatus (NEXUS_GAME_CALL *open)(uint64_t world, uint64_t* client); //< Managed用clientを作る。Native権限はEngineだけが発行する
    NexusGameStatus (NEXUS_GAME_CALL *close)(uint64_t client); //< 登録とqueueを失効させる。呼出し中の失効は拒否する
    NexusGameStatus (NEXUS_GAME_CALL *registerCommand)(uint64_t client, NexusGameString id, uint32_t version, NexusGameCommandFn callback, void* state);
    NexusGameStatus (NEXUS_GAME_CALL *invoke)(uint64_t client, NexusGameString id, uint32_t version, NexusEntityHandle target,
        NexusGameBytes request, uint8_t* response, uint32_t capacity, uint32_t* length); //< 一回で実行する。応答長照会での二重実行は禁止
    NexusGameStatus (NEXUS_GAME_CALL *publish)(uint64_t client, NexusGameString type, uint32_t version, NexusEntityHandle source, NexusGameBytes payload);
    NexusGameStatus (NEXUS_GAME_CALL *poll)(uint64_t client, NexusGameEventV1* header, uint8_t* buffer, uint32_t capacity, uint32_t* length); //< length=0 / OKは空。容量不足ならqueueを進めない
} NexusGameServicesApiV1;
/* 既存V1 Moduleに任意exportを追加する。create成功後、Managed開始より前に呼ばれる。 */
typedef struct NexusGameServicesContextV1 {
    uint32_t abiVersion, structSize;
    uint64_t world, client;
    NexusGameServicesApiV1 api;
} NexusGameServicesContextV1;
#define NEXUS_GAME_SERVICES_ENTRY "NexusConnectGameServices"
typedef NexusGameStatus (NEXUS_GAME_CALL *NexusConnectGameServicesFn)(void* state, const NexusGameServicesContextV1* context);
#pragma pack(pop)
#if defined(__cplusplus) && defined(_WIN64)
static_assert(sizeof(NexusGameBytes) == 16);
static_assert(sizeof(NexusGameEventV1) == 40);
static_assert(sizeof(NexusGameServicesApiV1) == 56);
static_assert(sizeof(NexusGameServicesContextV1) == 80);
#endif
