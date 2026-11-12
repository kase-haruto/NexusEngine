#pragma once
#include "GameApi.h"
#include "ScriptApi.h"
#include "ScriptPropertyApi.h"
#include "GameServices.h"
#include "NativeGameBindings.h"

#define NEXUS_MANAGED_BOOTSTRAP_ABI_VERSION 1u
#define NEXUS_MANAGED_GAMEPLAY_ABI_VERSION 2u
#pragma pack(push, 8)
/*-----------------------------------------------------------------------------------------
 * NexusManagedBootstrapContextV1
 * - Phase 3のBootstrapへWorld寿命tokenと共通Loggerだけを渡すInterop値
 * - Contextポインタは呼出し中だけ借用する。ECS・Entity・Native所有物のアドレスを渡さない
 *---------------------------------------------------------------------------------------*/
typedef struct NexusManagedBootstrapContextV1 {
    uint32_t abiVersion; //< Managed側も一致を要求するBootstrap ABI番号
    uint32_t structSize; //< Native / Managed間のLayout照合用byte数
    uint64_t world; //< Phase 2のWorld token。World停止後に失効する
    NexusGameStatus (NEXUS_GAME_CALL *log)(uint64_t world, NexusGameString message); //< Engine常駐コード。セッション停止でManaged参照を解除する
} NexusManagedBootstrapContextV1;

/* Managed entryはUnmanagedCallersOnly / Cdeclで公開し、例外をstatusへ変換する。
 * sessionはManaged所有状態の数値IDであり、GCHandleやNativeポインタではない。 */
typedef NexusGameStatus (NEXUS_GAME_CALL *NexusManagedStartFn)(const NexusManagedBootstrapContextV1* context, uint64_t* session);
typedef NexusGameStatus (NEXUS_GAME_CALL *NexusManagedStopFn)(uint64_t session);
/* Phase 3のV1を維持し、Gameplayで必要な関数表とAssembly配置だけをV2へ追加する。 */
typedef struct NexusManagedGameplayContextV2 {
    uint32_t abiVersion; //< 2を要求する。V1 Startとは別entryを使用する
    uint32_t structSize;
    uint64_t world;
    NexusGameApiV1 game;
    NexusScriptApiV1 scripts;
    NexusGameString gameAssembly; //< StartGameplay呼出し中だけ借用する絶対UTF-8パス
} NexusManagedGameplayContextV2;
typedef NexusGameStatus (NEXUS_GAME_CALL *NexusManagedGameplayStartFn)(const NexusManagedGameplayContextV2* context, uint64_t* session);
typedef NexusGameStatus (NEXUS_GAME_CALL *NexusManagedTickFn)(uint64_t session, NexusScriptBindingV1* bindings, uint32_t count, float deltaTime, uint32_t initializeOnly);
/*-----------------------------------------------------------------------------------------
 * NexusManagedGameplayContextV3
 * - V1 / V2のLayoutとentryを維持し、共通Component状態のcopy APIだけを追加する
 * - Contextと文字列は呼出し中だけ借用する。所有権・Native objectアドレスを渡さない
 *---------------------------------------------------------------------------------------*/
typedef struct NexusManagedGameplayContextV3 {
    uint32_t abiVersion; //< 3を要求する
    uint32_t structSize; //< x64で160 byte
    uint64_t world; //< セッション寿命で失効する共通World token
    NexusGameApiV1 game; //< 既存Entity / Transformの値API
    NexusScriptApiV1 scripts; //< 既存ScriptComponent関連付けAPI
    NexusGameString gameAssembly; //< 開始中だけ借用する絶対UTF-8パス
    NexusScriptPropertyApiV1 properties; //< 所有thread・attachmentを検証するcopy API
} NexusManagedGameplayContextV3;
typedef NexusGameStatus (NEXUS_GAME_CALL *NexusManagedReflectedStartFn)(const NexusManagedGameplayContextV3* context, uint64_t* session);
/* ReloadはBootstrap常駐entryだけを呼ぶ。Game側delegate / GCHandleをNativeに保持しない。
   * operation: 0=Prepare、1=Commit、2=Abort、3=Complete。Commit前のAbortは候補だけを取り消す。
   * Commit開始後のAbortは実行世代を停止する。suspendedは失敗時も有効な停止状態。 */
typedef NexusGameStatus (NEXUS_GAME_CALL *NexusManagedReloadFn)(uint64_t session, uint32_t operation, NexusGameString assembly, uint32_t* suspended);
/* V1～V3を変更せず、共通services表だけを追加する。clientはManagedの世代ごとに発行する。 */
typedef struct NexusManagedGameplayContextV4 {
    NexusManagedGameplayContextV3 gameplay;
    NexusGameServicesApiV1 services;
} NexusManagedGameplayContextV4;
typedef NexusGameStatus (NEXUS_GAME_CALL *NexusManagedHybridStartFn)(const NexusManagedGameplayContextV4* context, uint64_t* session);
/* V4を維持し、Engine常駐の型付きBinding dispatchだけをV5へ追加する。 */
typedef struct NexusManagedGameplayContextV5 {
    NexusManagedGameplayContextV4 gameplay;
    NexusNativeBindingApiV1 bindings;
} NexusManagedGameplayContextV5;
typedef NexusGameStatus (NEXUS_GAME_CALL *NexusManagedBoundStartFn)(const NexusManagedGameplayContextV5* context, uint64_t* session);
#pragma pack(pop)

#if defined(__cplusplus) && defined(_WIN64)
static_assert(sizeof(NexusManagedBootstrapContextV1) == 24);
static_assert(sizeof(NexusManagedGameplayContextV2) == 136);
static_assert(sizeof(NexusManagedGameplayContextV3) == 160);
static_assert(sizeof(NexusManagedGameplayContextV4) == 216);
static_assert(sizeof(NexusManagedGameplayContextV5) == 240);
#endif
