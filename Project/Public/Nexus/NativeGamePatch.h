#pragma once
#include "NativeGameBindings.h"
#pragma pack(push, 8)
/*-----------------------------------------------------------------------------------------
 * NexusNativePatchContractV1
 * - DLLをUnloadせず関数本体だけを更新するModuleの明示契約
 * - 永続stateは固定ヘッダーに置く。schemaは意味・所有権変更時も更新する
 * - サイズ一致だけで任意のC++レイアウト変更を安全と判定する契約ではない
 *---------------------------------------------------------------------------------------*/
typedef struct NexusNativePatchContractV1 {
    uint32_t abiVersion, structSize;
    uint64_t stateSchema; //< ゲーム側が維持する非0の固定state契約
    uint64_t moduleSize, moduleAlignment; //< Module stateの確保時layout
    uint64_t systemsLayout; //< 全Systemの固定layoutを表すゲーム側fingerprint
} NexusNativePatchContractV1;
#define NEXUS_NATIVE_PATCH_CONTRACT_ENTRY "NexusGetNativePatchContract"
#define NEXUS_NATIVE_REBIND_ENTRY "NexusRebindGameSystems"
typedef NexusGameStatus (NEXUS_GAME_CALL *NexusGetNativePatchContractFn)(uint32_t abi, NexusNativePatchContractV1* output);
/* 既存SystemをfindしてBindingだけを再登録する。create / destroyや新しい署名宣言を行わない。 */
typedef NexusGameStatus (NEXUS_GAME_CALL *NexusRebindGameSystemsFn)(void* state, const NexusNativeSystemsRegistrationV2* context);
/*-----------------------------------------------------------------------------------------
 * NexusNativePatchObserverV1
 * - Game DLL内のSDK hookからEngine常駐Backendへ結果だけを通知する
 * - contextはBackendの借用参照。Disable通知・解除exportの後はDLLから呼んではならない
 * - PrePatchのfalseはSDKの取消APIではない。DLL hookは不安全な適用前にProcessを終了する
 *---------------------------------------------------------------------------------------*/
typedef struct NexusNativePatchObserverV1 {
    uint32_t abiVersion, structSize;
    void* context; //< Managed参照ではなくEngine常駐Backendの非所有context
    uint32_t (NEXUS_GAME_CALL *notify)(void* context, uint32_t event, NexusGameString message); //< UTF-8診断は呼出し中だけ借用する
} NexusNativePatchObserverV1;
#define NEXUS_NATIVE_PATCH_PRE 1u
#define NEXUS_NATIVE_PATCH_POST 2u
#define NEXUS_NATIVE_PATCH_COMPILE_ERROR 3u
#define NEXUS_NATIVE_PATCH_LINK_ERROR 4u
#define NEXUS_NATIVE_PATCH_SOURCE 5u //< messageはSDKが報告した変更ファイルのUTF-8絶対パス
#define NEXUS_NATIVE_PATCH_OBSERVER_ENTRY "NexusSetNativePatchObserver"
/* nullで解除する。Moduleがロードされている所有Threadからだけ設定・解除する。 */
typedef NexusGameStatus (NEXUS_GAME_CALL *NexusSetNativePatchObserverFn)(const NexusNativePatchObserverV1* observer);
#pragma pack(pop)
#if defined(__cplusplus) && defined(_WIN64)
static_assert(sizeof(NexusNativePatchContractV1) == 40);
static_assert(sizeof(NexusNativePatchObserverV1) == 24);
#endif
