#pragma once
#include "Runtime/Project/ProjectContext.h"
#include <vector>

namespace NexusEngine {
enum class RecentProjectState { Available, Missing, Invalid };
struct RecentProject {
    std::filesystem::path file; //< マシン固有のDescriptor絶対パス
    std::string projectId, name; //< 最後の明示登録時の表示情報
    ProjectEngineRequirement engine; //< 最後に開いたEngine要求
    int64_t lastUsedUtc = 0; //< UTC Unix秒
    RecentProjectState state = RecentProjectState::Missing; //< 読込時に再検証する非永続状態
};

/*-----------------------------------------------------------------------------------------
 * ProjectRegistry
 * - ユーザーごとの最近使ったProjectを記録する
 * - Descriptor、Engineの選択、Runtime、UIの状態は変更しない
 *---------------------------------------------------------------------------------------*/
class ProjectRegistry final {
public:
    explicit ProjectRegistry(std::filesystem::path file) : file_(std::filesystem::absolute(file).lexically_normal()) {}
    /** \brief 保存先を取得する。LOCALAPPDATAがない場合はエラーにする */
    [[nodiscard]] static Result<std::filesystem::path> DefaultFile();
    /** \brief 一覧と実在状態を読む。破損Registryは空一覧へ読み替えない */
    [[nodiscard]] Result<std::vector<RecentProject>> Read() const;
    /** \brief 実在Projectを再検証し、最近使用日時と表示情報を記録する */
    [[nodiscard]] Result<void> Register(const ProjectContext& project) const;
    /** \brief 一覧の記録だけを削除する。Projectのファイルは削除しない */
    [[nodiscard]] Result<void> Remove(const std::filesystem::path& projectFile) const;
private:
    [[nodiscard]] Result<void> Save(const std::vector<RecentProject>& projects) const;
    std::filesystem::path file_; //< ユーザー固有のRegistry保存先
};
}
