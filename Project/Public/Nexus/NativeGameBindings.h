#pragma once
#include "NativeGameSystemApi.h"

#define NEXUS_GAME_SYSTEM_NOT_REGISTERED ((NexusGameStatus)7)
#define NEXUS_GAME_FUNCTION_NOT_REGISTERED ((NexusGameStatus)8)
#define NEXUS_GAME_BINDING_DISABLED ((NexusGameStatus)9)
#define NEXUS_GAME_STALE_BINDING ((NexusGameStatus)10) //< Engineがcallback実行前にだけ返す。再解決後の一度だけの再送が可能
#pragma pack(push, 8)
/*-----------------------------------------------------------------------------------------
 * NexusNativeValueV1
 * - 呼出し中だけ借用する固定layoutの値。未使用fieldは0、pointerは保存しない
 * - UTF8はbyte数、Float32Arrayは要素数をcountに指定し、最大4096 byteに限定する
 * - 戻り値のdataは呼出元のresultBufferだけを指す。DLL所有bufferを返してはならない
 *---------------------------------------------------------------------------------------*/
typedef struct NexusNativeValueV1 {
    uint32_t kind, count;
    int64_t integer; //< Bool / Int32 / UInt32 / Enum32。Enumの型IDは解決した署名で検証する
    float number[4]; //< Float32 / Vector2 / Vector3 / Vector4の値コピー
    NexusEntityHandle entity; //< Entity pointerではなく共通Worldの世代付きHandle
    const void* data; //< UTF8 / Float32Arrayの一時借用。空の場合はnullptr
} NexusNativeValueV1;
/* 数値scopeと世代だけを保持する。ModuleのコードやSystem objectを所有しない。 */
typedef struct NexusNativeBindingHandleV1 { uint64_t scope, system, function; } NexusNativeBindingHandleV1;
typedef NexusGameStatus (NEXUS_GAME_CALL *NexusNativeFunctionFn)(void* system, const NexusNativeValueV1* arguments, uint32_t count,
    NexusNativeValueV1* result, uint8_t* resultBuffer, uint32_t capacity);
/*-----------------------------------------------------------------------------------------
 * NexusNativeBindingRegistrationV1
 * - 宣言済み署名への明示Bindingと一時停止だけを担当する。Systemを生成しない
 * - 再登録は所有Threadのidle Scheduler上で停止中に行い、古い世代を復活させない
 *---------------------------------------------------------------------------------------*/
typedef struct NexusNativeBindingRegistrationV1 {
    uint32_t abiVersion, structSize;
    NexusGameStatus (NEXUS_GAME_CALL *bind)(uint64_t scope, uint64_t system, NexusGameString function, uint32_t version, NexusNativeFunctionFn callback);
    NexusGameStatus (NEXUS_GAME_CALL *setEnabled)(uint64_t scope, uint32_t enabled);
} NexusNativeBindingRegistrationV1;
typedef struct NexusNativeSystemsRegistrationV2 {
    NexusNativeSystemsRegistrationV1 registration; //< prefixのabiVersion=2 / structSize=96。V1 exportへそのまま渡さない
    NexusNativeBindingRegistrationV1 bindings;
} NexusNativeSystemsRegistrationV2;
#define NEXUS_NATIVE_SYSTEMS_ENTRY_V2 "NexusRegisterGameSystemsV2"
typedef NexusGameStatus (NEXUS_GAME_CALL *NexusRegisterGameSystemsV2Fn)(void* module, const NexusNativeSystemsRegistrationV2* context);
/*-----------------------------------------------------------------------------------------
 * NexusNativeBindingApiV1
 * - ManagedへはEngine常駐の解決・呼出し関数だけを渡す。DLL callbackはEngine内で解決する
 * - resolveはModule / System / Function / Versionと署名を照合し、invokeは登録世代を再検証する
 *---------------------------------------------------------------------------------------*/
typedef struct NexusNativeBindingApiV1 {
    uint32_t abiVersion, structSize;
    NexusGameStatus (NEXUS_GAME_CALL *resolve)(uint64_t world, NexusGameString module, NexusGameString system, uint32_t systemVersion,
        const NexusNativeFunctionDescriptorV1* signature, NexusNativeBindingHandleV1* output);
    NexusGameStatus (NEXUS_GAME_CALL *invoke)(uint64_t world, NexusNativeBindingHandleV1 handle, const NexusNativeValueV1* arguments, uint32_t count,
        NexusNativeValueV1* result, uint8_t* resultBuffer, uint32_t capacity);
} NexusNativeBindingApiV1;
#pragma pack(pop)
#if defined(__cplusplus) && defined(_WIN64)
static_assert(sizeof(NexusNativeValueV1) == 56);
static_assert(sizeof(NexusNativeBindingHandleV1) == 24);
static_assert(sizeof(NexusNativeBindingRegistrationV1) == 24);
static_assert(sizeof(NexusNativeSystemsRegistrationV2) == 96);
static_assert(sizeof(NexusNativeBindingApiV1) == 24);
#endif
