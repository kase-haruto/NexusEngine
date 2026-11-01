#pragma once
#include "Runtime/Project/ProjectService.h"
#include "Tools/Engine/EngineRegistry.h"
#include <vector>

namespace NexusEngine {
struct ProjectTemplate {
    std::string id, name, description;
    std::filesystem::path file;
};
/*-----------------------------------------------------------------------------------------
 * ProjectTemplates
 * - データ定義からテンプレートを列挙し、コピー・置換・Level検証を行う
 * - UI、プロセス起動、Engineの選択は担当しない
 *---------------------------------------------------------------------------------------*/
class ProjectTemplates final {
public:
    [[nodiscard]] static Result<std::vector<ProjectTemplate>> List(const std::filesystem::path& directory);
    [[nodiscard]] static Result<ProjectContext> Create(const std::filesystem::path& root, ProjectDescriptor descriptor,
        const ProjectTemplate& projectTemplate, const EngineInstallation& engine);
};
}
