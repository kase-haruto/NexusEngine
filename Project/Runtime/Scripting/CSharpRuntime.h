#pragma once
#include "CSharpRuntimeSettings.h"
#include "DotNetHost.h"
#include "NativeManagedBridge.h"

namespace NexusEngine {
class Level;
class ScriptSystem;
class GameApiBridge;
/*-----------------------------------------------------------------------------------------
 * CSharpRuntime
 * - GameRuntime所有のManaged sessionと公式Hostingの開始・終了順序を統括する
 * - EditorやImGuiに依存せず、Gameplay更新は既存LevelのScriptSystemへ接続する
 *---------------------------------------------------------------------------------------*/
class CSharpRuntime final {
public:
    CSharpRuntime() = default;
    ~CSharpRuntime();
    CSharpRuntime(const CSharpRuntime&) = delete;
    CSharpRuntime& operator=(const CSharpRuntime&) = delete;
    /**
     * \brief 公式.NET Hostingを開始する。LevelとWorldの対応・所有ThreadをHosting前に検証する
     * \param settings BootstrapとGameScriptsの明示配置。GameScripts空ならBootstrapだけを開始する
     * \param world 同じLevelのGameApiBridgeが発行した寿命token
     * \param level session停止まで生存する既存WorldとSchedulerの所有元
     * \return 部分開始・例外時も登録を解除してエラーを返す。停止失敗はquarantineで通知する
     */
    [[nodiscard]] Result<void> Start(const CSharpRuntimeSettings& settings, uint64_t world, Level& level);
    /** \brief 所有Threadのframe境界でSystemを解除し、Managed参照、Host参照の順に解放する。CLR自体はProcess寿命 */
    void Stop() noexcept;
    /** \brief Prepare成功後だけScene複製上で新世代を初期化する。失敗時は旧Sceneを復元する */
    [[nodiscard]] Result<void> Reload(const std::filesystem::path& assembly, Level& level, GameApiBridge& api);
    [[nodiscard]] bool IsSuspended() const noexcept { return reloadFailed_ || bridge_.IsSuspended(); }
    [[nodiscard]] bool IsQuarantined() const noexcept { return quarantined_; }
    [[nodiscard]] ManagedReloadState ReloadState() const noexcept { return reloadState_; }
private:
    DotNetHost host_; //< Bootstrap entryを取得する.NET Hosting adapter
    NativeManagedBridge bridge_; //< host_より先に停止するManaged session
    Level* level_ = nullptr; //< Schedulerの所有元を借用する。GameRuntimeはこのsessionより後にLevelを破棄する
    ScriptSystem* scripts_ = nullptr; //< Level Scheduler所有。StopでDetach・登録解除してから借用を落とす
    bool started_ = false; //< 二重開始を防ぎ、Bootstrapのlifecycleを一度ずつ実行する
    bool quarantined_ = false; //< 停止失敗後に同じRuntimeを再利用させない
    ManagedReloadState reloadState_ = ManagedReloadState::Idle; //< callbackやAssemblyを所有しない結果通知
    bool reloadFailed_ = false; //< Abortの失敗でもGameRuntimeを再開させない。正常な再適用でだけ解除する
};
}
