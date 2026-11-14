#pragma once
#include "GameServices.h"
#define NEXUS_DEBUG_COMMAND "nexus.engine.debug"
#define NEXUS_DEBUG_PROTOCOL_VERSION 1
#pragma pack(push, 8)
/** operation: 1=log(count: 0 Info/1 Warning/2 Error), 2=lines, 3=clear owner, 4=world matrix.
 * payloadはlogのUTF-8、またはcount個のLine。durationは非負秒、0は次の描画まで。
 * 1要求64線、1 Scene 1024線。参照を保持せず、全件検証してから追加する。 */
typedef struct NexusDebugRequestV1 {
    uint32_t operation, count;
    float duration;
    uint32_t reserved;
} NexusDebugRequestV1;
typedef struct NexusDebugLineV1 {
    float start[3], end[3], color[4];
    uint32_t depthTest;
} NexusDebugLineV1;
#pragma pack(pop)
#if defined(__cplusplus)
static_assert(sizeof(NexusDebugRequestV1) == 16);
static_assert(sizeof(NexusDebugLineV1) == 44);
#endif
