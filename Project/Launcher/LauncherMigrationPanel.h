#pragma once
#include "Tools/Project/ProjectMigration.h"
#include <functional>
#include <stop_token>
#include <memory>
#include <span>
#include <array>
#include <Windows.h>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * LauncherMigrationPanel
 * - Engine変更のレビュー・明示適用・復旧のUI状態を所有する
 * - workerの寿命とRegistryの更新はLauncherApplicationへ委譲する
 *---------------------------------------------------------------------------------------*/
class LauncherMigrationPanel final {
public:
    using Submit = std::function<void(std::string,std::function<Result<void>(std::stop_token)>)>;
    /** \brief Projectの移行画面を開く。明示targetはレビューだけを予約する */
    void Open(std::filesystem::path project, std::filesystem::path target = {});
    /** \brief UIスレッドだけで描画する。workerの結果はbusy解除後に取り込む */
    void Draw(HWND owner, std::span<const RegisteredEngine* const> engines, bool busy, const std::string& operationError, const Submit& submit);
private:
    void Review(const std::filesystem::path& target, const Submit& submit);
    std::filesystem::path project_, requestedTarget_;
    std::shared_ptr<std::optional<ProjectMigrationPlan>> review_ = std::make_shared<std::optional<ProjectMigrationPlan>>();
    std::shared_ptr<std::optional<std::filesystem::path>> applied_ = std::make_shared<std::optional<std::filesystem::path>>();
    std::shared_ptr<bool> restored_ = std::make_shared<bool>(false);
    std::array<char,4096> recovery_{};
    std::string message_;
    int selected_ = -1;
    bool open_ = false, accept_ = false;
};
}
