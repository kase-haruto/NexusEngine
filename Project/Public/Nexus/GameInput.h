#pragma once
#include "GameServices.h"
#define NEXUS_INPUT_COMMAND "nexus.engine.input"
#define NEXUS_INPUT_PROTOCOL_VERSION 1u
#define NEXUS_GAMEPAD_COUNT 4u
/* XInputの公開配置に合わせた共通button bit。OS固有objectは公開しない。 */
typedef enum NexusGamepadButton {
    NEXUS_PAD_DPAD_UP=0x0001, NEXUS_PAD_DPAD_DOWN=0x0002,
    NEXUS_PAD_DPAD_LEFT=0x0004, NEXUS_PAD_DPAD_RIGHT=0x0008,
    NEXUS_PAD_START=0x0010, NEXUS_PAD_BACK=0x0020,
    NEXUS_PAD_LEFT_STICK=0x0040, NEXUS_PAD_RIGHT_STICK=0x0080,
    NEXUS_PAD_LEFT_SHOULDER=0x0100, NEXUS_PAD_RIGHT_SHOULDER=0x0200,
    NEXUS_PAD_A=0x1000, NEXUS_PAD_B=0x2000, NEXUS_PAD_X=0x4000, NEXUS_PAD_Y=0x8000
} NexusGamepadButton;
#pragma pack(push, 8)
/*-----------------------------------------------------------------------------------------
 * NexusInputFrameV1
 * - 所有Windowから同じframeに採取された値コピー。空target/空requestへの応答
 * - key番号はWindows Virtual-Key。Native pointerやOS Windowを公開しない
 * - pressed/releasedはframe内で消費しないため、複数の言語・Systemから同じedgeを読める
 *---------------------------------------------------------------------------------------*/
typedef struct NexusInputFrameV1 {
    uint8_t down[256], pressed[256], released[256];
    float mouseX, mouseY, deltaX, deltaY, wheel;
    uint32_t focused, reserved[2];
} NexusInputFrameV1;
/*-----------------------------------------------------------------------------------------
 * NexusGamepadRequestV1 / NexusGamepadsFrameV1
 * - 同じInput V1 commandのoperation=1要求。空requestの800 byte応答は変更しない
 * - 4台分を一括コピーし、毎buttonのInteropやManaged側のNative device所有を防ぐ
 *---------------------------------------------------------------------------------------*/
typedef struct NexusGamepadRequestV1 { uint32_t operation; } NexusGamepadRequestV1;
typedef struct NexusGamepadStateV1 {
    uint32_t connected,connectedThisFrame,disconnectedThisFrame;
    uint32_t down,pressed,released;
    float leftStick[2],rightStick[2],leftTrigger,rightTrigger;
    uint32_t reserved[4];
} NexusGamepadStateV1;
typedef struct NexusGamepadsFrameV1 {
    uint32_t count,focused,reserved[2];
    NexusGamepadStateV1 pads[NEXUS_GAMEPAD_COUNT];
} NexusGamepadsFrameV1;
#pragma pack(pop)
#if defined(__cplusplus)
static_assert(sizeof(NexusInputFrameV1)==800);
static_assert(sizeof(NexusGamepadRequestV1)==4);
static_assert(sizeof(NexusGamepadStateV1)==64);
static_assert(sizeof(NexusGamepadsFrameV1)==272);
#endif
