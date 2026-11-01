#pragma once
#include "ProjectContext.h"
#include <functional>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * ProjectService
 * - Projectの作成・読込とフォルダ構造の検証を担当する
 * - GameコードやLevel形式を知らず、初期データ生成は呼出側に委譲する
 *---------------------------------------------------------------------------------------*/
class ProjectService final {
public:
    using Initializer = std::function<Result<void>(const ProjectContext&)>;
    /** \brief nxprojectを読み込み、必要なディレクトリと起動Levelを検証する */
    [[nodiscard]] static Result<ProjectContext> Open(const std::filesystem::path& file);
    /** \brief 新規RootだけにProjectを生成する。失敗時は自分のstagingだけを回収する
     * \param root 未作成のProject Root
     * \param descriptor 保存メタデータ。空のprojectIdにはUUIDを発行する
     * \param initialize staging内で初期Level等を作成する信頼済みの生成処理
     * \return 公開済みProject。既存Rootは空でも上書きしない
     */
    [[nodiscard]] static Result<ProjectContext> Create(const std::filesystem::path& root, ProjectDescriptor descriptor, const Initializer& initialize);
};
}
