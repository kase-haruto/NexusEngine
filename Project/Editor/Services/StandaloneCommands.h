#pragma once
#include <memory>
#include "Tools/Project/ProjectBuild.h"

namespace NexusEngine {
class EditorWorkspace;
class StandaloneSession;
/** \brief Project固有Runtimeのビルド・実行・停止をMenuとToolbarの同じCommandへ接続する */
void RegisterStandaloneCommands(EditorWorkspace& workspace, std::shared_ptr<StandaloneSession> session,
    ProjectLaunchOptions options = {});
}
