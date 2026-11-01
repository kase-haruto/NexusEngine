#pragma once
#include "Tools/Engine/EngineRegistry.h"
#include <vector>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * ProjectMigrationPlan
 * - 明示的なEngine変更の前提と確認事項を保持する
 * - AssetやC++ APIの自動変換を約束しない
 *---------------------------------------------------------------------------------------*/
struct ProjectMigrationPlan {
    std::filesystem::path projectFile, targetManifest;
    ProjectEngineRequirement previous, target;
    std::string projectId, descriptorHash, manifestHash, compatibilityHash;
    std::vector<std::string> levels; //< Root相対名と内容hashを連結したレビュー時点の集合
    std::vector<std::string> notes; //< UIとCLIに提示する互換性の確認事項
};
/*-----------------------------------------------------------------------------------------
 * ProjectMigration
 * - Engine変更の計画・明示適用・Descriptorの復旧を担当する
 * - バージョン解決失敗から自動実行せず、Asset/Level/Sourceは変更しない
 *---------------------------------------------------------------------------------------*/
class ProjectMigration final {
public:
    /** \brief 配置と保存形式を事前確認し、Projectを変更せず計画を返す */
    [[nodiscard]] static Result<ProjectMigrationPlan> Prepare(const std::filesystem::path& project, const std::filesystem::path& targetManifest);
    /** \brief 計画を新規ファイルへ保存する。既存計画を置換しない */
    [[nodiscard]] static Result<void> SavePlan(const std::filesystem::path& file, const ProjectMigrationPlan& plan);
    /** \brief 保存した計画を読む。適用時に現在の配置・形式・hashを再検証する */
    [[nodiscard]] static Result<ProjectMigrationPlan> LoadPlan(const std::filesystem::path& file);
    /** \brief API再ビルドの確認後、旧Descriptorと復旧記録を保存してEngine要求だけを変更する */
    [[nodiscard]] static Result<std::filesystem::path> Apply(const ProjectMigrationPlan& plan, bool acceptRebuild);
    /** \brief 適用直後のDescriptorだけを旧内容へ戻す。後から編集されていたら拒否する */
    [[nodiscard]] static Result<void> Restore(const std::filesystem::path& project, const std::filesystem::path& recovery);
};
}
