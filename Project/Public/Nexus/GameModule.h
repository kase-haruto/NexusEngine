#pragma once
#include "GameApi.h"

#define NEXUS_GAME_MODULE_ENTRY "NexusGetGameModule"
#pragma pack(push, 8)
/*-----------------------------------------------------------------------------------------
 * NexusGameModuleContextV1
 * - 一つの実行Worldと共通APIをModuleへ渡す開始時の値
 * - Moduleはapiを値コピーして保持できる。worldはStopで失効する数値Handle
 *---------------------------------------------------------------------------------------*/
typedef struct NexusGameModuleContextV1 {
    uint32_t abiVersion; //< Contextの互換性番号
    uint32_t structSize; //< Layout照合用のbyte数
    NexusGameApiV1 api; //< コピー可能なEngine関数表。Engine資源の所有権は移動しない
    uint64_t world; //< 一つの実行Worldの寿命token
} NexusGameModuleContextV1;

/*-----------------------------------------------------------------------------------------
 * NexusGameModuleV1
 * - Game DLL自身が提供するmetadataとlifecycle callback
 * - 例外、STL、CRT所有物を境界越しに渡さない。stateはDLL内で生成・破棄する
 *---------------------------------------------------------------------------------------*/
typedef struct NexusGameModuleV1 {
    uint32_t abiVersion; //< DLLが提供するABI番号
    uint32_t structSize; //< 期待するcallback配置との一致を検証するbyte数
    /* metadataの文字列はDLLの静的領域。EngineはUnloadより長く参照しない。 */
    NexusGameString engineId;
    NexusGameString engineVersion;
    NexusGameString projectId;
    NexusGameString moduleId;
    /* 失敗時もstateを返した場合はdestroyで解放できること。context自体を保持しない。 */
    NexusGameStatus (NEXUS_GAME_CALL *create)(const NexusGameModuleContextV1* context, void** state);
    NexusGameStatus (NEXUS_GAME_CALL *update)(void* state, float deltaTime);
    /* callback・Thread・Taskを全て停止してから成功を返す。失敗時EngineはDLLをUnloadしない。 */
    NexusGameStatus (NEXUS_GAME_CALL *destroy)(void* state);
} NexusGameModuleV1;

/* Entryはmetadataだけを返し、World変更やstate生成を行わない。
 * 全callbackは例外をDLL内でstatusへ変換する。DLL exportはextern C / __cdeclで定義する。 */
typedef NexusGameStatus (NEXUS_GAME_CALL *NexusGetGameModuleFn)(uint32_t requestedAbi, NexusGameModuleV1* output);
#pragma pack(pop)

#if defined(__cplusplus) && defined(_WIN64)
static_assert(sizeof(NexusGameModuleContextV1) == 80);
static_assert(sizeof(NexusGameModuleV1) == 96);
#endif
