#pragma once
#include "GameServices.h"
#define NEXUS_PHYSICS_COMMAND "nexus.engine.physics"
#define NEXUS_PHYSICS_PROTOCOL_VERSION 1u
#define NEXUS_PHYSICS_CONTACT_EVENT "nexus.engine.physics.contacts"
#define NEXUS_PHYSICS_CONTACT_PAGE_CAPACITY 63u
#pragma pack(push, 8)
/*-----------------------------------------------------------------------------------------
 * NexusPhysicsRequestV1
 * - operationは1=Raycast、2=Force、3=Impulse、4=接触snapshot、5=既存Eventへの通知購読、6=layer mask付きRaycast
 * - operation=6ではreserved2が32bit layer mask。既存のoperation=1は全layerを照会する
 * - Raycastは空target、他は生存Entityを要求する。vectorは原点または力、directionはRaycastだけ
 * - reservedは4でoffset、5で購読0/1、他は0。pointer/STL/solver固有型を含めない
 *---------------------------------------------------------------------------------------*/
typedef struct NexusPhysicsRequestV1 {
    uint32_t operation, reserved;
    float vector[3], direction[3], distance;
    uint32_t reserved2;
} NexusPhysicsRequestV1;
/*-----------------------------------------------------------------------------------------
 * NexusPhysicsHitV1
 * - Raycast結果の値コピー。無衝突時は全fieldが0（entity.world=0）
 * - Entityは同じWorldの既存世代付きHandle。Jolt Body IDは公開しない
 *---------------------------------------------------------------------------------------*/
typedef struct NexusPhysicsHitV1 {
    NexusEntityHandle entity;
    float point[3], normal[3], distance;
    uint32_t reserved;
} NexusPhysicsHitV1;
/* 接触はEntity pair単位。Exitでは破棄済み世代のHandleも含む。normalはfirstからsecond向き。 */
typedef struct NexusPhysicsContactV1 {
    NexusEntityHandle first,second;
    float point[3],normal[3];
    uint32_t kind,sensor; //< 1=Enter、2=Stay、3=Exit。sensorはuint32の0/1
} NexusPhysicsContactV1;
/* このheaderの後にcount件のcontactを連結する。next==UINT32_MAXで終端。欠落はdroppedで通知する。 */
typedef struct NexusPhysicsContactsHeaderV1 { uint32_t count,next,dropped,reserved; } NexusPhysicsContactsHeaderV1;
#pragma pack(pop)
#if defined(__cplusplus)
static_assert(sizeof(NexusPhysicsRequestV1)==40);
static_assert(sizeof(NexusPhysicsHitV1)==48);
static_assert(sizeof(NexusPhysicsContactV1)==64);
static_assert(sizeof(NexusPhysicsContactsHeaderV1)==16);
#endif
