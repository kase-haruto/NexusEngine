#pragma once
#include "Tools/Project/ProjectBuild.h"
#include <memory>
namespace NexusEngine {
class EditorWorkspace;
/** \brief Module追加・Source作成・Build・Packageを共通Project panelとCommandへ登録する */
void RegisterProjectDevelopmentCommands(EditorWorkspace& workspace, std::shared_ptr<const ProjectContext> project,
    EngineInstallation engine, ProjectBuildOptions options = {});
}
