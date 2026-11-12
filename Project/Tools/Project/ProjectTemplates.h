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
    /**
     * \brief Templateの初期Module群と共通データをstagingへ生成する。呼出元のUI状態を保持しない
     * \param root 新規Projectの配置先
     * \param descriptor 共通Project情報。初期生成ではmodulesは空にする
     * \param projectTemplate 共通Levelと初期Moduleのファイル定義
     * \param engine 選択したEngine。既定Engineへの暗黙の切替は行わない
     * \param initialLanguage 最初のModuleの実装言語。後から追加するModuleを制限しない
     * \return 公開済みProject、または既存データを変更しない生成エラー
     */
    [[nodiscard]] static Result<ProjectContext> Create(const std::filesystem::path& root, ProjectDescriptor descriptor,
        const ProjectTemplate& projectTemplate, const EngineInstallation& engine,
        ProjectModuleLanguage initialLanguage = ProjectModuleLanguage::Cpp);
};
}
