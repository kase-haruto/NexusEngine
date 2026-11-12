#pragma once
#include "GameServices.h"

/* 既存GameServicesの値通信を使う限定Engine protocol。関数表V1～V5のlayoutは変更しない。
 * IDとversionを明示し、Game DLLがEngineの組込み処理を上書きすることは禁止する。 */
#define NEXUS_COMPONENT_COMMAND "nexus.engine.components"
#define NEXUS_COMPONENT_PROTOCOL_VERSION 1u
#define NEXUS_TRANSFORM_BATCH_COMMAND "nexus.engine.transforms"

enum NexusComponentOperation {
    NEXUS_COMPONENT_HAS = 1, NEXUS_COMPONENT_ADD = 2, NEXUS_COMPONENT_REMOVE = 3,
    NEXUS_COMPONENT_READ = 4, NEXUS_COMPONENT_WRITE = 5, NEXUS_COMPONENT_QUERY = 6,
    NEXUS_COMPONENT_ATTACHMENT = 7, NEXUS_COMPONENT_WRITE_ATTACHED = 8, NEXUS_COMPONENT_READ_ATTACHED = 9
};
enum NexusComponentValueKind {
    NEXUS_COMPONENT_BOOL = 1, NEXUS_COMPONENT_INT = 2, NEXUS_COMPONENT_FLOAT = 3,
    NEXUS_COMPONENT_STRING = 4, NEXUS_COMPONENT_VECTOR2 = 5, NEXUS_COMPONENT_VECTOR3 = 6,
    NEXUS_COMPONENT_VECTOR4 = 7, NEXUS_COMPONENT_ENUM = 8
};
#pragma pack(push, 8)
/*-----------------------------------------------------------------------------------------
 * NexusComponentValueV1
 * - 共通PropertyValueの境界用コピー。未使用fieldは必ず0とする
 * - StringはこのDTOの後ろにUTF-8 byteを連結し、pointer / STL / GC参照を渡さない
 *---------------------------------------------------------------------------------------*/
typedef struct NexusComponentValueV1 {
    uint32_t kind, stringLength; //< 明示kindとUTF-8 byte数。NUL終端は含まない
    int32_t integer; //< Boolは0 / 1、Int / Enumは符号付き32bit
    uint32_t reserved; //< 将来のlayout拡張を暗黙に受け入れないため0を要求する
    float number[4]; //< Float / Vectorの値。有限値だけを許可する
} NexusComponentValueV1;
/*-----------------------------------------------------------------------------------------
 * NexusComponentRequestV1
 * - 後続byteをType ID → Property ID → String値の順で連結する
 * - Queryのoffsetは0または前回のnextOffsetを渡すopaque cursor。構造変更を挟まずページを読む
 *---------------------------------------------------------------------------------------*/
typedef struct NexusComponentRequestV1 {
    uint32_t operation, typeLength, propertyLength; //< 安定Type / Property ID。class名推測は行わない
    uint32_t offset, limit, reserved; //< Queryはcursor / 件数。ATTACHED操作ではtokenの下位 / 上位32bit
    NexusComponentValueV1 value; //< Write / WriteAttachedだけが使用し、他操作では全fieldを0とする
} NexusComponentRequestV1;
/* Query応答はheaderの後ろにcount個のNexusEntityHandleをコピーする。
 * nextOffset=UINT32_MAXは終端。Has応答はuint32_t 0 / 1、Read応答はValueとStringの連結。
 * Attachment応答はuint64_t tokenであり、旧Engineの未知操作は成功扱いにしない。 */
typedef struct NexusComponentQueryV1 { uint32_t count, nextOffset; } NexusComponentQueryV1;
/*-----------------------------------------------------------------------------------------
 * NexusTransformBatchV1 / NexusTransformUpdateV1
 * - 既存Transformの一括コピー。operation=1はHandle配列入力とTransform配列応答
 * - operation=2はUpdate配列入力。最大64件をすべて検証してから一括適用する
 *---------------------------------------------------------------------------------------*/
typedef struct NexusTransformBatchV1 { uint32_t operation, count; } NexusTransformBatchV1;
typedef struct NexusTransformUpdateV1 {
    NexusEntityHandle entity; //< 毎回Worldと世代を検証し、配列内にpointerを含めない
    NexusLocalTransform transform; //< Local値のみ。派生Matrixは既存TransformSystemが計算する
} NexusTransformUpdateV1;
#pragma pack(pop)
#if defined(__cplusplus) && defined(_WIN64)
static_assert(sizeof(NexusComponentValueV1) == 32);
static_assert(sizeof(NexusComponentRequestV1) == 56);
static_assert(sizeof(NexusComponentQueryV1) == 8);
static_assert(sizeof(NexusTransformBatchV1) == 8);
static_assert(sizeof(NexusTransformUpdateV1) == 56);
#endif
