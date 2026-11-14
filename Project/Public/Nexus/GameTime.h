#pragma once
#include "GameServices.h"
#define NEXUS_TIME_COMMAND "nexus.engine.time"
#define NEXUS_REFERENCE_COMMAND "nexus.engine.references"
#pragma pack(push, 8)
/** operation:1=clock snapshot、2=TimeScale設定(0～100)、3=FixedDeltaTime設定(.001～1秒)。設定は次frameへ適用。 */
typedef struct NexusTimeRequestV1 { uint32_t operation, reserved; float value; uint32_t reserved2; } NexusTimeRequestV1;
typedef struct NexusGameTimeV1 {
    double elapsed, unscaledElapsed, fixedElapsed;
    float deltaTime, unscaledDeltaTime, fixedDeltaTime, timeScale;
    uint64_t frame, fixedTick;
    uint32_t inFixedStep, reserved;
    double droppedFixedTime;
} NexusGameTimeV1;
/** references: operation=1はtargetのID取得、2はIDから現在WorldのEntityを解決。未発見は空Handle。 */
typedef struct NexusEntityReferenceRequestV1 { uint32_t operation, reserved; uint64_t high, low; } NexusEntityReferenceRequestV1;
typedef struct NexusEntityReferenceV1 { uint64_t high, low; } NexusEntityReferenceV1;
#pragma pack(pop)
#if defined(__cplusplus)
static_assert(sizeof(NexusGameTimeV1) == 72);
static_assert(sizeof(NexusTimeRequestV1) == 16);
static_assert(sizeof(NexusEntityReferenceRequestV1) == 24);
#endif
