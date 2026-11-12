#pragma once
#include "Tools/Project/ProjectRegistry.h"
#include "Tools/Project/ProjectTemplates.h"
#include "Tools/Engine/EngineRegistry.h"
#include "LauncherMigrationPanel.h"
#include <future>
#include <functional>
#include <stop_token>
#include <array>
#include <Windows.h>

namespace NexusEngine {
struct LauncherEngine {
    RegisteredEngine registration;
    std::vector<ProjectTemplate> templates;
};
struct LauncherSnapshot {
    std::vector<RecentProject> projects;
    std::vector<LauncherEngine> engines;
    std::optional<EngineInstallation> defaultEngine;
    std::string warning; //< 既定Engineの破損があっても修復用一覧は表示する
};
/*-----------------------------------------------------------------------------------------
 * LauncherApplication
 * - Project Browserの表示と操作を既存のProject/Engineサービスへ接続する
 * - 遅いIO・ハッシュ・ビルドは一つのworkerで実行し、UIへsnapshotを返す
 *---------------------------------------------------------------------------------------*/
class LauncherApplication final {
public:
    LauncherApplication(HWND owner, std::filesystem::path recentFile, std::filesystem::path engineFile, int page = 0, bool smoke = false);
    ~LauncherApplication();
    /** \brief UIを描画し、完了したworkerの結果を取り込む */
    void Draw();
    [[nodiscard]] bool Busy() const { return task_.valid(); }
    [[nodiscard]] bool HasError() const { return !error_.empty(); }
    /** \brief 外部からレビュー画面だけを開く。変更の適用はUIで明示する */
    void ReviewEngine(std::filesystem::path project, std::filesystem::path target = {}) { migration_.Open(std::move(project),std::move(target)); }
private:
    using Operation = std::function<Result<void>(std::stop_token)>;
    void Begin(std::string message, Operation operation);
    void Refresh();
    void OpenProject(std::filesystem::path file);
    void PickProject();
    void DrawProjects();
    void DrawNewProject();
    void DrawEngines();
    void PickPath(std::array<char,4096>& buffer, bool folder);
    HWND owner_; //< Windowの非所有参照
    const std::filesystem::path recentFile_, engineFile_;
    const bool smoke_; //< 自動検証時だけ子Editorも非表示の有限frameで起動する
    LauncherSnapshot snapshot_; //< workerと共有しない表示用snapshot
    std::future<Result<LauncherSnapshot>> task_;
    std::stop_source stop_;
    std::string working_, error_;
    std::array<char,256> search_{}, name_{};
    std::array<char,4096> location_{}, manifest_{}, development_{};
    int tab_ = 0, engine_ = -1, template_ = 0, configuration_ = 0;
    int initialLanguage_ = 0; //< UI threadが所有する初期Moduleの選択。Projectの固定言語ではない
    bool focusSearch_ = false;
    LauncherMigrationPanel migration_; //< 移行操作の表示状態。Projectやworkerを所有しない
};
}
