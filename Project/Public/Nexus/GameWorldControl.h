#pragma once
#include "GameServices.h"
#define NEXUS_WORLD_CONTROL_COMMAND "nexus.engine.world"
/*-----------------------------------------------------------------------------------------
 * NexusWorldRequestV1
 * - 1=次Levelの要求、2=Project内asset file存在確認。後続はUTF-8相対参照
 * - Level交換は所有Runtimeがcallback終了後のframe境界で行う。Editor documentは変更しない
 * - 既存Project path解決だけを利用し、新しいAsset ManagerやLevel loaderを作らない
 *---------------------------------------------------------------------------------------*/
typedef struct NexusWorldRequestV1 { uint32_t operation,pathLength; } NexusWorldRequestV1;
#if defined(__cplusplus)
static_assert(sizeof(NexusWorldRequestV1)==8);
#endif
