#pragma once
#include "Tools/Engine/EngineRegistry.h"
#include "Runtime/Project/ProjectContext.h"
#include "Runtime/Scripting/CSharpRuntimeSettings.h"
#include <memory>
namespace NexusEngine {
class EditorWorkspace;
/** \brief 同じCommand / Process基盤へManaged Compile・Reloadを登録する。CLRの所有権はRuntimeへ残す */
void RegisterManagedReloadCommands(EditorWorkspace& workspace, std::shared_ptr<const ProjectContext> project,
                                  EngineInstallation engine, CSharpRuntimeSettings settings);
}
