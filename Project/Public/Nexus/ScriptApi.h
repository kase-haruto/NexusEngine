#pragma once
#include "GameApi.h"
#pragma pack(push, 8)
/* EntityのComponent関連付けの寿命と有効状態。ManagedへNative Componentアドレスを返さない。 */
typedef struct NexusScriptStateV1 {
    uint64_t attachment; //< 同じEntityでも削除・再追加後は異なるtoken
    uint32_t enabled; //< ABIへboolを渡さない
    uint32_t reserved;
} NexusScriptStateV1;
/*-----------------------------------------------------------------------------------------
 * NexusScriptApiV1
 * - 共通ECSのScriptComponentを操作する明示API。別のScript用ECSを持たない
 * - 変更は現在のcallback後または次のScript同期でlifecycleへ反映する
 *---------------------------------------------------------------------------------------*/
typedef struct NexusScriptApiV1 {
    uint32_t abiVersion;
    uint32_t structSize;
    /** \brief 主Scriptを設定・置換する。追加Scriptは維持し、主Scriptのtokenだけ更新する */
    NexusGameStatus (NEXUS_GAME_CALL *setScript)(NexusEntityHandle entity, NexusGameString type, uint32_t enabled);
    /** \brief 従来互換として全Scriptを解除する。個別解除はScriptAttachments protocolを使う */
    NexusGameStatus (NEXUS_GAME_CALL *removeScript)(NexusEntityHandle entity);
    /** \brief 既存関連付けを解決する。編集World由来の未採番Componentにはtokenを発行する */
    NexusGameStatus (NEXUS_GAME_CALL *getState)(NexusEntityHandle entity, NexusScriptStateV1* output);
    /** \brief 関連付けを維持したまま有効状態だけを変更する */
    NexusGameStatus (NEXUS_GAME_CALL *setEnabled)(NexusEntityHandle entity, uint32_t enabled);
} NexusScriptApiV1;
/* callback中だけ借用する一括同期値。instanceだけをManagedが出力する。 */
typedef struct NexusScriptBindingV1 {
    NexusEntityHandle entity;
    NexusGameString type;
    uint64_t attachment;
    uint64_t instance;
    uint32_t enabled;
    uint32_t reserved;
} NexusScriptBindingV1;
#pragma pack(pop)
#if defined(__cplusplus) && defined(_WIN64)
static_assert(sizeof(NexusScriptStateV1) == 16);
static_assert(sizeof(NexusScriptApiV1) == 40);
static_assert(sizeof(NexusScriptBindingV1) == 56);
#endif
