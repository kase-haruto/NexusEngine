#pragma once
#include "NativeGameSystems.h"
#pragma pack(push, 8)
/* 公開値契約。PropertyKindと重なる値は同じ値モデルを使うが、内部enumの数値ABIには依存しない。 */
enum NexusGameValueKind {
    NEXUS_VALUE_VOID = 0, NEXUS_VALUE_BOOL = 1, NEXUS_VALUE_INT32 = 2, NEXUS_VALUE_UINT32 = 3,
    NEXUS_VALUE_FLOAT32 = 4, NEXUS_VALUE_VECTOR2 = 5, NEXUS_VALUE_VECTOR3 = 6, NEXUS_VALUE_VECTOR4 = 7,
    NEXUS_VALUE_UTF8 = 8, NEXUS_VALUE_ENTITY = 9, NEXUS_VALUE_ENUM32 = 10,
    NEXUS_VALUE_FLOAT32_ARRAY = 11
};
typedef struct NexusGameApiTypeV1 {
    uint32_t kind, reserved;
    NexusGameString typeId; //< Enumのみ必須。型のアドレスでなく永続IDを宣言する
} NexusGameApiTypeV1;
typedef struct NexusGameApiParameterV1 { NexusGameString id; NexusGameApiTypeV1 type; } NexusGameApiParameterV1;
/*-----------------------------------------------------------------------------------------
 * NexusNativeFunctionDescriptorV1
 * - Systemが公開する関数ID・Version・値型・thread契約だけを宣言する
 * - この宣言には呼出先を含めない。明示Bindingは別契約でありクラス全体を公開しない
 *---------------------------------------------------------------------------------------*/
typedef struct NexusNativeFunctionDescriptorV1 {
    uint32_t structSize, version;
    NexusGameString id;
    NexusGameApiTypeV1 result;
    const NexusGameApiParameterV1* parameters;
    uint32_t parameterCount, ownerThreadOnly; //< ownerThreadOnlyは1だけを許可する
} NexusNativeFunctionDescriptorV1;
typedef struct NexusNativeFunctionInfoV1 {
    uint64_t function, system; //< scopeと組で有効な数値登録世代。ポインタではない
    uint32_t version, parameterCount, returnKind, ownerThreadOnly;
} NexusNativeFunctionInfoV1;
/* System生成・所有を行わない公開Metadata用の別関数表。既存byte-command APIへ自動登録しない。 */
typedef struct NexusNativeSystemPublicApiV1 {
    uint32_t abiVersion, structSize;
    NexusGameStatus (NEXUS_GAME_CALL *declareFunction)(uint64_t scope, uint64_t system, const NexusNativeFunctionDescriptorV1* descriptor);
    NexusGameStatus (NEXUS_GAME_CALL *findFunction)(uint64_t scope, uint64_t system, NexusGameString id, uint32_t version, NexusNativeFunctionInfoV1* output);
} NexusNativeSystemPublicApiV1;
/*-----------------------------------------------------------------------------------------
 * NexusNativeSystemsRegistrationV1
 * - Game ModuleにSystem登録とAPI契約宣言の別窓口を渡す
 * - scopeはModuleの初期登録後にsealedとなり、find / accessだけが継続利用できる
 *---------------------------------------------------------------------------------------*/
typedef struct NexusNativeSystemsRegistrationV1 {
    uint32_t abiVersion, structSize;
    uint64_t scope;
    NexusNativeSystemsApiV1 systems;
    NexusNativeSystemPublicApiV1 publications;
} NexusNativeSystemsRegistrationV1;
#define NEXUS_NATIVE_SYSTEMS_ENTRY "NexusRegisterGameSystems"
typedef NexusGameStatus (NEXUS_GAME_CALL *NexusRegisterGameSystemsFn)(void* module, const NexusNativeSystemsRegistrationV1* context);
#pragma pack(pop)
#if defined(__cplusplus) && defined(_WIN64)
static_assert(sizeof(NexusGameApiTypeV1) == 24);
static_assert(sizeof(NexusGameApiParameterV1) == 40);
static_assert(sizeof(NexusNativeFunctionDescriptorV1) == 64);
static_assert(sizeof(NexusNativeFunctionInfoV1) == 32);
static_assert(sizeof(NexusNativeSystemPublicApiV1) == 24);
static_assert(sizeof(NexusNativeSystemsRegistrationV1) == 72);
#endif
