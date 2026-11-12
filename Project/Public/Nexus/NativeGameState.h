#pragma once
#include "GameApi.h"
#define NEXUS_NATIVE_SAVE_STATE_ENTRY "NexusSaveNativeGameState"
#define NEXUS_NATIVE_RESTORE_STATE_ENTRY "NexusRestoreNativeGameState"
#define NEXUS_NATIVE_STATE_ABI_VERSION 1u
#define NEXUS_NATIVE_STATE_MAX_BYTES 1048576u
/*-----------------------------------------------------------------------------------------
 * NexusSaveNativeGameStateFn / NexusRestoreNativeGameStateFn
 * - Moduleが明示選択したゲーム値だけをEngine所有bufferへ保存・復元する任意exportの対
 * - Saveは純粋な読み取り。容量0で必要長と非0 schemaを返し、その後同じ内容をコピーする
 * - pointer/allocator/GPU資源のdumpは禁止。Restoreはschemaを検証し、失敗しても破棄可能なstateを保つ
 * - Live++の固定root layout契約を緩めない。SystemやModuleのrootアドレスを置換しない
 * - 全呼出しは所有threadの更新停止境界。bufferは呼出し中だけ借用し、DLL内へ保持しない
 *---------------------------------------------------------------------------------------*/
typedef NexusGameStatus (NEXUS_GAME_CALL *NexusSaveNativeGameStateFn)(void* state,uint32_t abi,
    uint64_t* schema,uint8_t* output,uint32_t capacity,uint32_t* length);
typedef NexusGameStatus (NEXUS_GAME_CALL *NexusRestoreNativeGameStateFn)(void* state,uint32_t abi,
    uint64_t schema,const uint8_t* data,uint32_t length);
