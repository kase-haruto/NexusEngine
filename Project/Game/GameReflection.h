#pragma once
#include "Runtime/Reflection/ReflectionRegistration.h"
#include "Game/Generated/GeneratedGameReflection.h"

namespace NexusGame {
/** \brief EngineとGameの型を結合する。重複IDはComponentReflectionが拒否する */
inline void RegisterComponents(NexusEngine::ComponentReflection& registry) {
    NexusEngine::RegisterGeneratedComponents(registry);
    NexusEngine::RegisterGameComponents(registry);
}
/** \brief Detailsへ公開する型を同じ構成で登録する */
inline void RegisterInspector(NexusEngine::TypeRegistry& registry) {
    NexusEngine::RegisterGeneratedInspectorTypes(registry);
    NexusEngine::RegisterGameInspectorTypes(registry);
}
/** \brief Level読込前にホストから渡す、不変の登録方針を作る */
inline NexusEngine::ReflectionRegistration SampleReflection() {
    return {RegisterComponents, RegisterInspector};
}
}
