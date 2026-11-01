#pragma once
#include "ProjectDescriptor.h"
#include "Foundation/Error/Result.h"
#include <filesystem>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * ProjectSerializer
 * - nxprojectのJSON形式とスキーマを検証し、原子的に保存する
 * - Projectフォルダの作成、最近のProject、UIは管理しない
 *---------------------------------------------------------------------------------------*/
class ProjectSerializer final {
public:
    /** \brief Descriptorの値と相対パスを検証する \return 不正項目を含むエラー */
    [[nodiscard]] static Result<void> Validate(const ProjectDescriptor& descriptor);
    /** \brief UTF-8のnxprojectを読み込む \param path Descriptorファイル \return 検証済みメタデータ */
    [[nodiscard]] static Result<ProjectDescriptor> Load(const std::filesystem::path& path);
    /** \brief 検証後に保存する。失敗時は旧ファイルを保持する \param replace falseなら既存ファイルを上書きしない */
    [[nodiscard]] static Result<void> Save(const std::filesystem::path& path, const ProjectDescriptor& descriptor, bool replace = true);
};
}
