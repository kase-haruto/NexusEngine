#pragma once
#include "GameApi.h"
#pragma pack(push, 8)
/*-----------------------------------------------------------------------------------------
 * NexusScriptPropertyApiV1
 * - 共通ScriptComponentの永続状態を呼出側所有UTF-8 bufferへコピーする
 * - attachmentで削除・型置換後の旧instance書込を拒否する。Nativeポインタの保持は禁止する
 *---------------------------------------------------------------------------------------*/
typedef struct NexusScriptPropertyApiV1 {
    uint32_t abiVersion; //< 1を要求する
    uint32_t structSize; //< x64では24 byte
    NexusGameStatus (NEXUS_GAME_CALL *read)(NexusEntityHandle entity, uint64_t attachment, char* output, uint32_t capacity, uint32_t* length); //< NUL終端なし。容量不足時も必要byte数を返す
    NexusGameStatus (NEXUS_GAME_CALL *write)(NexusEntityHandle entity, uint64_t attachment, NexusGameString state); //< 検証完了後だけ原子的に置換する
} NexusScriptPropertyApiV1;
#pragma pack(pop)
#if defined(__cplusplus) && defined(_WIN64)
static_assert(sizeof(NexusScriptPropertyApiV1) == 24);
#endif
