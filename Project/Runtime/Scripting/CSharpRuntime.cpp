#include "CSharpRuntime.h"
#include "Runtime/Gameplay/GameApiBridge.h"
#include "Runtime/Gameplay/NativeGameSystemScope.h"
#include "ScriptSystem.h"
#include "Runtime/Level/Level.h"
#include "Runtime/Project/ProjectJson.h"
#include "Runtime/Scene/TransformSystem.h"
#include <limits>
#include <exception>
#include "Foundation/Logging/Logger.h"

namespace NexusEngine {
CSharpRuntime::~CSharpRuntime() { Stop(); }

Result<void> CSharpRuntime::Start(const CSharpRuntimeSettings& settings, uint64_t world, Level& level) {
    if(started_ || scripts_ || level_ || quarantined_ || !world || !level.GetScene() || level.GetSystemScheduler().IsExecuting() || !GameApiBridge::MatchesWorld(level, world))
        return std::unexpected(Error(ErrorCategory::Framework, 232, "CSharpRuntime requires a stopped session, idle Scheduler and matching World on its owner thread."));
    // 呼出元のGameRuntimeだけに解放を任せず、直接利用でも部分開始・例外時に登録とManaged参照を残さない。
    struct StartCleanup {
        CSharpRuntime& runtime; bool committed = false;
        ~StartCleanup() { if(!committed) runtime.Stop(); }
    } cleanup{*this};
    try {
        auto hosted = host_.Initialize(settings); if(!hosted) return hosted;
        Result<void> started;
        if(settings.gameAssembly.empty()) {
            const NexusManagedBootstrapContextV1 context{NEXUS_MANAGED_BOOTSTRAP_ABI_VERSION, sizeof(NexusManagedBootstrapContextV1), world, GameApiBridge::Api().log};
            started = bridge_.Start(host_, settings.bootstrapAssembly, context);
        } else {
            if(!settings.gameAssembly.is_absolute() || !std::filesystem::is_regular_file(settings.gameAssembly)) {
                return std::unexpected(Error(ErrorCategory::Framework, 232, "GameScripts requires an existing absolute assembly path."));
            }
            const auto path = ProjectJson::Utf8(settings.gameAssembly);
            if(path.size() > (std::numeric_limits<uint32_t>::max)()) return std::unexpected(Error(ErrorCategory::Framework, 232, "GameScripts path is too long."));
            const NexusManagedGameplayContextV5 context{{{5, sizeof(NexusManagedGameplayContextV5), world, GameApiBridge::Api(), GameApiBridge::Scripts(),
                {path.data(), static_cast<uint32_t>(path.size())}, GameApiBridge::Properties()}, GameApiBridge::Services()}, NativeGameSystemScope::BindingApi()};
            started = bridge_.StartBoundGameplay(host_, settings.bootstrapAssembly, context);
        }
        if(!started) return started;
        if(!settings.gameAssembly.empty()) {
            // Systemの所有権は既存Schedulerへ渡す。借用先を記録してからInitializeし、失敗時もDetach可能にする。
            level_ = &level;
            scripts_ = &level.GetSystemScheduler().AddSystem<ScriptSystem>(level, bridge_, world);
            auto initialized = scripts_->Initialize();
            if(!initialized) return initialized;
        }
        started_ = true;
        NEXUS_LOG_INFO("CSharpRuntime", settings.gameAssembly.empty() ? "Managed bootstrap session started." : "Managed gameplay session started.");
        cleanup.committed = true;
        return {};
    } catch(const std::exception& exception) {
        return std::unexpected(Error(ErrorCategory::Framework, 232, std::string("Managed session startup failed: ") + exception.what()));
    } catch(...) {
        return std::unexpected(Error(ErrorCategory::Framework, 232, "Managed session startup failed with an unknown exception."));
    }
}
void CSharpRuntime::Stop() noexcept {
    if(scripts_) {
        // Bridge破棄後に呼ばれないよう先に無効化する。同じLevelを再利用しても無効なSystemを蓄積しない。
        scripts_->Detach();
        if(!level_ || !level_->GetSystemScheduler().RemoveSystem(*scripts_)) {
            // 実行中のadapterは削除しない。通常の停止はGameRuntimeがframe境界へ遅延する。
            quarantined_ = true;
            NEXUS_LOG_ERROR("CSharpRuntime", "Cannot remove ScriptSystem during execution; restart the host process.");
        }
        scripts_ = nullptr;
    }
    level_ = nullptr;
    quarantined_ |= !bridge_.Stop();
    if(quarantined_) GameApiBridge::BlockLegacyPatches();
    host_.Shutdown();
    quarantined_ |= host_.IsQuarantined();
    started_ = false;
    reloadFailed_ = false;
}
Result<void> CSharpRuntime::Reload(const std::filesystem::path& assembly, Level& level, GameApiBridge& api) {
    reloadState_ = ManagedReloadState::CompatibilityError;
    if(quarantined_) {
        reloadState_ = ManagedReloadState::StateRestoreError;
        return std::unexpected(Error(ErrorCategory::Framework, 235, "Managed Runtime is quarantined; restart the host process before reloading."));
    }
    if(!started_ || !scripts_ || level_ != &level || level.GetSystemScheduler().IsExecuting() || !GameApiBridge::MatchesWorld(level, api.World()) ||
        !assembly.is_absolute() || !std::filesystem::is_regular_file(assembly))
        return std::unexpected(Error(ErrorCategory::Framework, 235, "Reload requires active Gameplay and an existing absolute candidate path."));
    if(!api.FreezeManagedServices(true)) return std::unexpected(Error(ErrorCategory::Framework, 236, "Cannot freeze Native services for Reload."));
    struct Unfreeze { GameApiBridge& api; ~Unfreeze() { static_cast<void>(api.FreezeManagedServices(false)); } } unfreeze{api};
    // 適用前の失敗は互換性エラー、世代停止後の失敗は状態復元エラーとして区別する。
    bool completed = false; bool retiring = false;
    struct Status {
        ManagedReloadState& state; bool& completed; bool& retiring; bool& failed;
        ~Status() {
            state = completed ? ManagedReloadState::Succeeded : retiring ? ManagedReloadState::StateRestoreError : ManagedReloadState::CompatibilityError;
            if(completed) failed = false; else if(retiring) failed = true;
        }
    } status{reloadState_, completed, retiring, reloadFailed_};
    reloadState_ = ManagedReloadState::Reloading;
    auto prepared = bridge_.Reload(0, assembly);
    if(!prepared) {
        // 候補cleanupの失敗でもManaged側が停止する場合がある。旧世代継続可能な互換性エラーと区別する。
        retiring = bridge_.IsSuspended();
        return prepared;
    }
    // Prepare後のClone失敗・例外でも候補shadowだけを取り消す。旧世代を止めるCommitとは分ける。
    struct PreparedCleanup {
        NativeManagedBridge& bridge; bool owned = true;
        ~PreparedCleanup() { if(owned) { try { static_cast<void>(bridge.Reload(2)); } catch(...) {} } }
    } preparedCleanup{bridge_};
    // 既存SceneのCloneを再利用し、OnCreate失敗でEntity / Componentを失わないようにする。
    auto candidate = level.GetScene()->Clone();
    if(!candidate) return std::unexpected(Error(ErrorCategory::Framework, 235, "Cannot clone the World for Managed Reload."));
    auto original = level.ExchangeScene(std::move(candidate));
    const auto reconnect = [&] { return api.RebindScene() && scripts_->RebindScene(); };
    bool committed = false;
    // Commit後だけManaged callbackを停止してから元Worldへ戻す。Prepare取消では旧Behaviourを維持する。
    struct Restore {
        Level& level; GameApiBridge& api; ScriptSystem& scripts; NativeManagedBridge& bridge;
        std::unique_ptr<Scene>& original; bool& committed; bool& retiring; bool& failed;
        ~Restore() {
            if(committed) return;
            try { if(!bridge.Reload(2) && retiring) failed = true; } catch(...) { if(retiring) failed = true; }
            level.ExchangeScene(std::move(original));
            const bool worldConnected = api.RebindScene();
            // Commit前は旧instance IDも復元する。Commit後は旧Behaviourがないため再利用しない。
            const bool scriptsConnected = scripts.RebindScene(retiring);
            if(!worldConnected || !scriptsConnected) failed = true;
        }
    } restore{level, api, *scripts_, bridge_, original, committed, retiring, reloadFailed_};
    preparedCleanup.owned = false; // Scene交換後の取消責務はRestoreへ移す。
    if(!reconnect()) return std::unexpected(Error(ErrorCategory::Framework, 235, "Cannot connect the Reload World."));
    retiring = true;
    auto activated = bridge_.Reload(1); if(!activated) return activated;
    auto initialized = scripts_->Initialize(); if(!initialized) return initialized;
    auto transforms = TransformSystem{}.Update(*level.GetScene()); if(!transforms) return transforms;
    auto complete = bridge_.Reload(3); if(!complete) return complete;
    committed = true;
    // 新世代確定後の接続失敗を成功通知しない。旧世代へ戻したことにもせず、このWorldを停止状態で保持する。
    if(!api.FreezeManagedServices(false)) {
        reloadFailed_ = true;
        quarantined_ = true; // 再開不能な境界を同一Processの新sessionへ持ち越さない。
        return std::unexpected(Error(ErrorCategory::Framework, 236, "Managed generation committed, but Native services could not resume. World retained; restart the host process."));
    }
    completed = true;
    NEXUS_LOG_INFO("CSharpRuntime", "Managed GameScripts reloaded; state restored and old context unload confirmed.");
    return {};
}
}
