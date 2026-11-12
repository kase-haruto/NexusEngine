#pragma once
#include "GameApi.h"
/*-----------------------------------------------------------------------------------------
 * NexusGetGameComponentMetadataFn
 * - DLL初期化以外のゲーム処理を開始せず、値ComponentのJSON Metadataだけを返す任意export
 * - 出力UTF-8はDLL所有。EngineはUnload前に上限1 MiBでコピーし、pointerを保存しない
 * - Game Moduleの実体生成・System登録・.NET所有はこのexportの責務ではない
 *---------------------------------------------------------------------------------------*/
#define NEXUS_GAME_COMPONENT_METADATA_ENTRY "NexusGetGameComponentMetadata"
typedef NexusGameStatus (NEXUS_GAME_CALL *NexusGetGameComponentMetadataFn)(uint32_t abi, NexusGameString* output);
