#pragma once
// Descriptor.reservedのbit 0。固定stepでもupdateを呼び、時計のinFixedStepで区別する。
#define NEXUS_NATIVE_SYSTEM_FIXED_UPDATES 1u
#include "GameApi.h"
#pragma pack(push, 8)
/*-----------------------------------------------------------------------------------------
 * NexusNativeSystemContextV1
 * - Scheduler所有adapterからDLL所有Systemへ、共通Worldと値APIだけを渡す
 * - contextはcreate中だけ借用する。System Handleはscope失効で無効になり再利用しない
 *---------------------------------------------------------------------------------------*/
typedef struct NexusNativeSystemContextV1 {
    uint32_t abiVersion, structSize;
    NexusGameApiV1 game;
    uint64_t world, system;
} NexusNativeSystemContextV1;
/*-----------------------------------------------------------------------------------------
 * NexusNativeSystemDescriptorV1
 * - Systemの実体生成・実行・破棄を定義する。公開関数の登録とは独立する
 * - stateはDLL内で確保・解放し、Engineはadapterを既存Schedulerへ登録する
 *---------------------------------------------------------------------------------------*/
typedef struct NexusNativeSystemDescriptorV1 {
    uint32_t structSize, version;
    NexusGameString id;
    const NexusGameString* after; //< 同じModuleで先に登録済みのSystem ID。前方参照・循環を拒否する
    uint32_t afterCount, reserved; //< reservedはNEXUS_NATIVE_SYSTEM_FIXED_UPDATESのみ許可。他のbitは0。
    const NexusGameString* reads; //< 既存ComponentReflectionのType ID。RTTI / STLを越境させない
    uint32_t readCount, reservedReads;
    const NexusGameString* writes;
    uint32_t writeCount, reservedWrites;
    NexusGameStatus (NEXUS_GAME_CALL *create)(const NexusNativeSystemContextV1* context, void** state);
    NexusGameStatus (NEXUS_GAME_CALL *update)(void* state, float deltaTime);
    NexusGameStatus (NEXUS_GAME_CALL *destroy)(void* state);
} NexusNativeSystemDescriptorV1;
/* Nativeのみの一時借用。System pointerはcallback外へ保存せず、C#へ渡さない。 */
typedef NexusGameStatus (NEXUS_GAME_CALL *NexusNativeSystemVisitorFn)(void* system, void* user);
/*-----------------------------------------------------------------------------------------
 * NexusNativeSystemsApiV1
 * - Moduleの初期登録、数値Handle検索、Nativeの限定借用を分けて提供する
 * - 実行・所有は既存Scheduler。自動Bindingや別System Managerを構築しない
 *---------------------------------------------------------------------------------------*/
typedef struct NexusNativeSystemsApiV1 {
    uint32_t abiVersion, structSize;
    NexusGameStatus (NEXUS_GAME_CALL *add)(uint64_t scope, const NexusNativeSystemDescriptorV1* descriptor, uint64_t* system);
    NexusGameStatus (NEXUS_GAME_CALL *find)(uint64_t scope, NexusGameString id, uint32_t version, uint64_t* system);
    NexusGameStatus (NEXUS_GAME_CALL *access)(uint64_t scope, uint64_t system, NexusNativeSystemVisitorFn visitor, void* user);
} NexusNativeSystemsApiV1;
#pragma pack(pop)
#if defined(__cplusplus) && defined(_WIN64)
static_assert(sizeof(NexusNativeSystemContextV1) == 88);
static_assert(sizeof(NexusNativeSystemDescriptorV1) == 96);
static_assert(sizeof(NexusNativeSystemsApiV1) == 32);
#endif
