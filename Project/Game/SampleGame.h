#pragma once
#include "Runtime/Application/GameRuntime.h"
#include "GameReflection.h"

namespace NexusGame {
/** \brief Editor/単体起動で共通のサンプルSystemを登録する。ゲーム固有処理はGame層に置く */
[[nodiscard]] NexusEngine::Result<void> SetupSampleGame(NexusEngine::Level& level);
}
