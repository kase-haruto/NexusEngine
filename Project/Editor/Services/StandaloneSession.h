#pragma once
#include "Tools/Project/ProjectBuild.h"
#include <memory>
#include <future>
#include <chrono>

namespace NexusEngine {
struct StandaloneState {
    bool running = false; //< 現在の子Runtimeが実行中か
    uint32_t processId = 0; //< 自分が起動したRuntimeのPID
    std::optional<uint32_t> exitCode; //< 終了済みの場合だけ保持する結果
    bool building = false; //< Runtimeの個別ビルドを待機中
    bool stopping = false; //< ビルド取消または通常終了を要求済み
    std::filesystem::path buildLog, runtimeLog; //< この実行の新規ログ
    std::string error; //< 直近の起動・実行失敗
};
/*-----------------------------------------------------------------------------------------
 * StandaloneSession
 * - 別プロセスRuntimeの非同期ビルドを委譲し、起動・終了要求・状態を管理する
 * - Worldを操作せず、Editor終了時も独立Runtimeを強制終了しない
 *---------------------------------------------------------------------------------------*/
class StandaloneSession final {
public:
    /** \brief 開いたProjectと明示選択済みEngineのsnapshotを保持する */
    StandaloneSession(std::shared_ptr<const ProjectContext> project, EngineInstallation engine)
        : project_(std::move(project)), engine_(std::move(engine)) {}
    ~StandaloneSession();
    /** \brief 二重起動を拒否し、Projectの検証済みRuntimeを起動する */
    [[nodiscard]] Result<void> Start(const ProjectLaunchOptions& options = {});
    /** \brief Runtimeだけを非同期で増分ビルドし、Pollで検証済み成果物を起動する */
    [[nodiscard]] Result<void> RequestStart(const ProjectLaunchOptions& options = {});
    /** \brief UIスレッドから完了結果と通常終了を処理する。失敗は一度だけ返す */
    [[nodiscard]] Result<void> Poll();
    /** \brief 自分が起動したRuntimeだけへ通常終了を要求する */
    [[nodiscard]] Result<void> Stop();
    /** \brief 実際のプロセス状態と終了codeを返す */
    [[nodiscard]] Result<StandaloneState> State() const;
private:
    std::shared_ptr<const ProjectContext> project_; //< 開いたProjectの不変snapshot
    EngineInstallation engine_; //< 明示選択済みのEngine
    std::optional<Process> child_; //< handleだけを所有し、終了時にRuntimeをkillしない
    std::future<Result<std::filesystem::path>> build_; //< workerはビルドだけを行いUIやWorldを操作しない
    std::stop_source cancelBuild_; //< 自分のビルドJobだけを取り消す
    ProjectLaunchOptions pendingOptions_; //< ビルド完了後の明示的な起動条件
    std::filesystem::path buildLog_, runtimeLog_; //< 上書きしない実行ごとのログ
    std::string error_; //< UIへ返す失敗の内容
    bool stopping_ = false; //< 自分のRuntimeへ通常終了を要求済み
    bool closeSent_ = false; //< WM_CLOSE送信後の重複送信を防ぐ
    bool exitReported_ = false; //< 非ゼロ終了の通知を一度に制限する
    std::chrono::steady_clock::time_point closeDeadline_; //< Window初期化中の終了要求を再試行する期限
};
}
