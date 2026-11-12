#pragma once
#include "ScriptApi.h"
#define NEXUS_SCRIPT_ATTACHMENTS_COMMAND "nexus.engine.scripts"
#define NEXUS_SCRIPT_ATTACHMENTS_PROTOCOL_VERSION 1u
#pragma pack(push, 8)
/*-----------------------------------------------------------------------------------------
 * NexusScriptAttachmentRequestV1
 * - 既存servicesの追加protocol。従来ScriptApiV1/Bootstrapの関数表layoutは変更しない
 * - 1=冪等追加、2=token状態、3=token有効化、4=token解除、5=一覧、6=同型の独立instance追加
 * - 応答は1/2/5がScriptStateV1、5だけ後続UTF-8型ID。状態なしはtoken=0
 *---------------------------------------------------------------------------------------*/
typedef struct NexusScriptAttachmentRequestV1 {
    uint32_t operation, enabled, typeLength, index; //< 未使用fieldは0。型IDは追加時だけ後続へ連結する
    uint64_t attachment; //< 2/3/4で必須。0や解除済みtokenを現在のScriptへ読み替えない
} NexusScriptAttachmentRequestV1;
#pragma pack(pop)
#if defined(__cplusplus)
static_assert(sizeof(NexusScriptAttachmentRequestV1) == 24);
#endif
