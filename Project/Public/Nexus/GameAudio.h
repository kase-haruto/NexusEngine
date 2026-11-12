#pragma once
#include "GameServices.h"
#define NEXUS_AUDIO_COMMAND "nexus.engine.audio"
#define NEXUS_AUDIO_PROTOCOL_VERSION 1u
#pragma pack(push, 8)
/*-----------------------------------------------------------------------------------------
 * NexusAudioRequestV1
 * - 既存servicesの値protocol。1=Play、2=Stop、3=IsPlaying、4=SetVolume
 * - Playはheader後にProject相対WAV pathのUTF-8を連結する。応答はuint64 Voice Handle
 * - IsPlayingの応答もuint64で0/1。Stop/SetVolumeには応答payloadがない
 * - Voice Handleは発行clientに属し、client失効時に全voiceを停止する
 *---------------------------------------------------------------------------------------*/
typedef struct NexusAudioRequestV1 {
    uint32_t operation, pathLength;
    uint64_t voice;
    float volume;
    uint32_t loop;
} NexusAudioRequestV1;
#pragma pack(pop)
#if defined(__cplusplus)
static_assert(sizeof(NexusAudioRequestV1)==24);
#endif
