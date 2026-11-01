#pragma once
#include "Runtime/Project/ProjectContext.h"
#include "Tools/Engine/EngineRegistry.h"
#include "Platform/Process/Process.h"
#include <stop_token>

namespace NexusEngine {
enum class ProjectHostKind { Editor, Runtime };
struct ProjectBuildTargets {
    std::filesystem::path editorProject, runtimeProject, editorExecutable, runtimeExecutable;
};
struct ProjectLaunchOptions {
    bool checkOnly = false; //< GPUを使わない既存CPU検証
    bool hidden = false; //< ウィンドウとコンソールを表示しない
    uint32_t frames = 0; //< 0は通常実行。有限frameは明示的な検証用
    std::filesystem::path log; //< 任意の新規絶対パス。既存ログは置換しない
};
/*-----------------------------------------------------------------------------------------
 * ProjectBuild
 * - 選択EngineでProject自身のホストをビルドし、成果物の結び付きを検証する
 * - Engineを勝手に選択せず、EditorやGraphicsの内部を操作しない
 *---------------------------------------------------------------------------------------*/
class ProjectBuild final {
public:
    [[nodiscard]] static Result<ProjectBuildTargets> Targets(const ProjectContext& project, const std::string& configuration);
    [[nodiscard]] static Result<std::filesystem::path> Build(const ProjectContext& project, const EngineInstallation& engine,
        ProjectHostKind kind, const std::filesystem::path& log, std::stop_token stop = {});
    [[nodiscard]] static Result<void> Verify(const ProjectContext& project, const EngineInstallation& engine, const std::filesystem::path& executable,
                                            ProjectHostKind kind = ProjectHostKind::Runtime);
    [[nodiscard]] static Result<EngineInstallation> Binding(const ProjectContext& project, const std::filesystem::path& executable,
                                                           const std::filesystem::path& explicitManifest = {});
    [[nodiscard]] static Result<Process> Launch(const ProjectContext& project, const EngineInstallation& engine, ProjectHostKind kind,
        bool checkOnly = false);
    /** \brief 非表示・有限frame等の明示オプションで同じProject起動経路を利用する */
    [[nodiscard]] static Result<Process> Launch(const ProjectContext& project, const EngineInstallation& engine, ProjectHostKind kind,
        const ProjectLaunchOptions& options);
};
}
