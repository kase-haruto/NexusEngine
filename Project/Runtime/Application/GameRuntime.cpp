#include "GameRuntime.h"
#include "Runtime/Gameplay/GameApiBridge.h"
#include "Runtime/Reflection/NativeComponentMetadata.h"
#include "Runtime/Scripting/CSharpRuntime.h"
#include "Runtime/Scene/Components/ScriptComponent.h"
#include "Runtime/Scene/TransformSystem.h"
#include "Foundation/Logging/Logger.h"
#include "Platform/FileSystem/AtomicFile.h"
#include <cmath>
#include <exception>
#include <algorithm>

namespace NexusEngine {
GameRuntime::GameRuntime() = default;
GameRuntime::~GameRuntime() {
    // Backendの完了通知なしに所有者が消える場合、適用有無を推測してDLLを解放しない。
    if(nativePatchPrepared_ && nativeModule_) { nativeModule_->AbandonPatch(); nativePatchPrepared_ = false; }
    Stop();
}
bool GameRuntime::IsCSharpSuspended() const noexcept { return csharpRuntime_ && csharpRuntime_->IsSuspended(); }
ManagedReloadState GameRuntime::CSharpReloadState() const noexcept { return csharpRuntime_ ? csharpRuntime_->ReloadState() : ManagedReloadState::Idle; }
void GameRuntime::TogglePause() noexcept {
    if(!OnOwnerThread() || !level_ || IsCSharpSuspended() || nativePatchPrepared_ || nativePatchFailed_) return;
    try {
        auto result=level_->PauseAudio(!paused_);
        if(!result) { NEXUS_LOG_ERROR("Audio",result.error().GetMessageText()); return; }
        paused_=!paused_;
    } catch(...) { /* noexcept境界では状態を反転せず、現在の停止状態を保つ。 */ }
}

bool GameRuntime::SetNativeGameModule(NativeGameModuleSettings settings) {
    if(!OnOwnerThread() || level_ || starting_ || stopping_ || nativeQuarantined_ || managedQuarantined_) return false;
    nativeSettings_ = std::move(settings);
    additionalNativeSettings_.clear();
    return true;
}
bool GameRuntime::SetNativeGameModules(std::vector<NativeGameModuleSettings> settings) {
    if(!OnOwnerThread() || level_ || starting_ || stopping_ || nativeQuarantined_ || managedQuarantined_ || settings.size() > 256) return false;
    // 複数DLLの通常実行と、単一DLLに限定したLive++契約は別に検証する。
    if(settings.size() > 1 && std::ranges::any_of(settings, [](const auto& item) { return item.livePatch; })) return false;
    for(size_t index = 0; index < settings.size(); ++index)
        for(size_t previous = 0; previous < index; ++previous) if(settings[index].moduleId == settings[previous].moduleId || SameFilePath(settings[index].file, settings[previous].file)) return false;
    nativeSettings_.reset(); additionalNativeSettings_.clear();
    if(!settings.empty()) {
        nativeSettings_ = std::move(settings.front());
        for(size_t index = 1; index < settings.size(); ++index) additionalNativeSettings_.push_back(std::move(settings[index]));
    }
    return true;
}
bool GameRuntime::SetCSharpRuntime(CSharpRuntimeSettings settings) {
    if(!OnOwnerThread() || level_ || starting_ || stopping_ || nativeQuarantined_ || managedQuarantined_) return false;
    managedSettings_ = std::move(settings);
    return true;
}
bool GameRuntime::SetLevelSetup(GameLevelSetup setup) {
    if(!OnOwnerThread() || level_ || starting_ || stopping_) return false;
    setup_ = std::move(setup);
    return true;
}

Result<void> GameRuntime::Start(std::unique_ptr<Level> level) {
    if(!OnOwnerThread()) return std::unexpected(Error(ErrorCategory::Framework, 45, "GameRuntime Start requires its owner thread."));
    if(nativeQuarantined_ || managedQuarantined_)
        return std::unexpected(Error(ErrorCategory::Framework, 221, "A previous game code session did not shut down cleanly. Restart the host process before starting another World."));
    if(level_ || starting_ || updating_ || stopping_ || !level || !level->GetScene())
        return std::unexpected(Error(ErrorCategory::Framework, 40, "Game runtime requires a loaded level and a stopped session."));
    // 設定の値受渡しは開始前に完了できるが、Worldを所有する最初の開始要求以降はThreadを変えない。
    if(thread_ == std::thread::id{}) thread_ = std::this_thread::get_id();
    starting_ = true;
    nativePatchState_ = NativePatchState::Idle;
    pendingStop_ = false;
    // Setupの失敗・例外で半構築のLevelを公開しない。古い編集Worldも変更しない。
    struct Reset { bool& flag; ~Reset() { flag = false; } } reset{starting_};
    try {
        if(setup_) {
            auto result = setup_(*level);
            if(!result) return result;
        }
        // Setupの取消はDLL検査より先に確定する。検査のLoadLibraryもDllMainを実行するため、
        // 取消済みWorldでは純粋Metadataの取得を含めてModule由来の処理を開始しない。
        if(pendingStop_) return std::unexpected(Error(ErrorCategory::Framework, 234, "Game startup was cancelled during setup."));
        if(!level->GetScene())
            return std::unexpected(Error(ErrorCategory::Framework, 41, "Game setup unloaded its level."));
        if(nativeSettings_) {
            // ProjectHostはLevel読込前にCatalogを取得済み。ここではSetup成功後かつBridge生成前に
            // 宣言を再検証し、直接Runtimeを起動する経路にも同じ値Metadataを接続する。
            std::vector<NativeGameModuleSettings> modules{*nativeSettings_};
            modules.insert(modules.end(), additionalNativeSettings_.begin(), additionalNativeSettings_.end());
            auto metadata = NativeComponentMetadata::Inspect(modules); if(!metadata) return std::unexpected(metadata.error());
            const auto previous = level->GetReflectionRegistration().nativeComponents;
            if(previous && !previous->SameModules(**metadata))
                return std::unexpected(Error(ErrorCategory::Framework, 247, "Native Component metadata changed after Editor startup. Rebuild and restart the host."));
            level->reflection_.nativeComponents = *metadata;
            nativeSettings_->componentMetadata = (*metadata)->ModuleSchema(nativeSettings_->moduleId); nativeSettings_->componentMetadataChecked = true;
            for(auto& settings : additionalNativeSettings_) { settings.componentMetadata = (*metadata)->ModuleSchema(settings.moduleId); settings.componentMetadataChecked = true; }
        }
        if(pendingStop_) return std::unexpected(Error(ErrorCategory::Framework, 247, "Game startup cancelled during Native metadata inspection."));
        bool hasScripts = false;
        level->GetScene()->ForEach<ScriptComponent>([&](Entity, const ScriptComponent&) { hasScripts = true; });
        if(hasScripts && (!managedSettings_ || managedSettings_->gameAssembly.empty()))
            return std::unexpected(Error(ErrorCategory::Framework, 234, "ScriptComponent requires a Managed Gameplay host and GameScripts assembly."));
        // 構築中だけlocalへ所有し、Module開始失敗でも編集Worldを公開・変更しない。
        auto api = (nativeSettings_ || managedSettings_) ? std::make_unique<GameApiBridge>(*level) : nullptr;
        auto module = nativeSettings_ ? std::make_unique<GameModuleLoader>() : nullptr;
        std::vector<std::unique_ptr<GameModuleLoader>> additional;
        auto managed = managedSettings_ ? std::make_unique<CSharpRuntime>() : nullptr;
        // 途中のreturn / 例外でもModule破棄結果をRuntimeへ記録してからBridgeを解放する。
        struct ConstructionCleanup {
            std::unique_ptr<GameModuleLoader>& moduleOwner; bool& quarantinedFlag;
            std::unique_ptr<CSharpRuntime>& managedOwner; bool& managedQuarantinedFlag;
            std::vector<std::unique_ptr<GameModuleLoader>>& additionalOwners;
            ~ConstructionCleanup() {
                if(managedOwner) { managedOwner->Stop(); managedQuarantinedFlag |= managedOwner->IsQuarantined(); }
                for(auto item = additionalOwners.rbegin(); item != additionalOwners.rend(); ++item) { (*item)->Stop(); quarantinedFlag |= (*item)->IsQuarantined(); }
                if(moduleOwner) { moduleOwner->Stop(); quarantinedFlag |= moduleOwner->IsQuarantined(); }
            }
        } cleanup{module, nativeQuarantined_, managed, managedQuarantined_, additional};
        if(module) {
            const NexusGameModuleContextV1 context{NEXUS_GAME_ABI_VERSION, sizeof(NexusGameModuleContextV1), GameApiBridge::Api(), api->World()};
            auto started = module->Start(*nativeSettings_, context, level.get());
            if(!started) { nativeQuarantined_ |= module->IsQuarantined(); return started; }
        }
        for(const auto& settings : additionalNativeSettings_) {
            if(pendingStop_) return std::unexpected(Error(ErrorCategory::Framework, 234, "Native startup was cancelled."));
            additional.push_back(std::make_unique<GameModuleLoader>());
            const NexusGameModuleContextV1 context{NEXUS_GAME_ABI_VERSION, sizeof(NexusGameModuleContextV1), GameApiBridge::Api(), api->World()};
            auto started = additional.back()->Start(settings, context, level.get());
            if(!started) return started;
        }
        if(managed) {
            // Native API公開をすべて終えてからManagedのOnCreateへ進む。
            if(pendingStop_) return std::unexpected(Error(ErrorCategory::Framework, 234, "Native startup requested Stop before Managed initialization."));
            auto hosted = managed->Start(*managedSettings_, api->World(), *level);
            if(!hosted) return hosted;
        }
        if(pendingStop_) return std::unexpected(Error(ErrorCategory::Framework, 234, "Game startup was cancelled by a callback stop request."));
        // 初期Matrixだけを確定する。ゲームSystemをUpdate(0)で余分に実行しない。
        auto transforms = TransformSystem{}.Update(*level->GetScene());
        if(!transforms) return transforms;
        level_ = std::move(level);
        gameApi_ = std::move(api);
        nativeModule_ = std::move(module);
        additionalNativeModules_ = std::move(additional);
        csharpRuntime_ = std::move(managed);
        paused_ = false;
        // DLLがBackendへ参照を渡す前に所有権を確定する。接続失敗・例外もStop / pinの経路へ戻す。
        // Agentより早く起動するStandaloneの登録は保留し、Agent生成後に同じ経路で接続する。
        if(nativeSettings_ && nativeSettings_->livePatch) {
            if(nativeSettings_->patchSourceDirectory.empty() || !nativeSettings_->registerPatchRuntime || !nativeSettings_->unregisterPatchRuntime ||
                !nativeSettings_->registerPatchRuntime(*this, *nativeSettings_)) {
                starting_ = false; Stop();
                return std::unexpected(Error(ErrorCategory::Framework, 240, "Module-only Live++ Backend is unavailable, busy or missing its DLL observer contract."));
            }
            livePatchRegistered_ = true;
        }
        return {};
    } catch(const std::exception& exception) {
        return std::unexpected(Error(ErrorCategory::Framework, 42, std::string("Game level setup failed: ") + exception.what()));
    } catch(...) {
        return std::unexpected(Error(ErrorCategory::Framework, 42, "Game level setup failed with an unknown exception."));
    }
}

void GameRuntime::Stop() noexcept {
    // 誤ThreadのUpdate失敗や終了要求から、System / DLL / CLRを別Threadで破棄しない。
    // ここではLogger callbackにも再入せず、所有Threadへ停止責務を残す。
    if(!OnOwnerThread()) return;
    if(stopping_) return;
    if(starting_ || updating_ || nativePatchPrepared_) { pendingStop_ = true; return; }
    stopping_ = true;
    struct ResetStopping { bool& flag; ~ResetStopping() { flag = false; } } reset{stopping_};
    // SDKにはロード中のDLLを通知する。解除失敗時は以降のdestroy / FreeLibraryを許可しない。
    if(livePatchRegistered_) {
        if(!nativeSettings_->unregisterPatchRuntime(*this) && nativeModule_) nativeModule_->AbandonPatch();
        livePatchRegistered_ = false;
    }
    // createで取得したHandleをdestroyまで使用できるよう、ModuleをWorldより先に停止する。
    if(csharpRuntime_) { csharpRuntime_->Stop(); managedQuarantined_ |= csharpRuntime_->IsQuarantined(); }
    csharpRuntime_.reset();
    for(auto item = additionalNativeModules_.rbegin(); item != additionalNativeModules_.rend(); ++item) { (*item)->Stop(); nativeQuarantined_ |= (*item)->IsQuarantined(); }
    additionalNativeModules_.clear();
    if(nativeModule_) { nativeModule_->Stop(); nativeQuarantined_ |= nativeModule_->IsQuarantined(); }
    nativeModule_.reset();
    gameApi_.reset();
    level_.reset();
    paused_ = false;
    pendingStop_ = false;
    nativePatchPrepared_ = false; nativePatchFailed_ = false;
}

Result<void> GameRuntime::Update(float deltaTime) {
    auto updated=UpdateWorld(deltaTime); if(!updated || !level_ || IsPaused() || IsCSharpSuspended()) return updated;
    auto requested=level_->TakeLevelRequest(); if(!requested) return {};
    try {
        // callbackのstackとScheduler実行guardが戻った後だけ次のWorldを準備する。
        auto next=std::make_unique<Level>("Level",level_->GetReflectionRegistration());
        auto root=next->SetResourceRoot(level_->ResourceRoot()); if(!root) return root;
        if(!next->SetPhysicsModelSource(level_->PhysicsModels())) return std::unexpected(Error(ErrorCategory::Resource,260,"Cannot configure the next Level collision assets."));
        auto loaded=next->Load(*requested);
        if(!loaded) { NEXUS_LOG_ERROR("Level",loaded.error().GetMessageText()); return {}; }
        std::vector<std::shared_ptr<void>> code;
        const auto retain=[&](GameModuleLoader& module)->Result<void> {
            auto image=module.RetainCode(); if(!image) return std::unexpected(image.error());
            code.push_back(std::move(*image)); return {};
        };
        if(nativeModule_) { auto kept=retain(*nativeModule_); if(!kept) return kept; }
        for(auto& module:additionalNativeModules_) { auto kept=retain(*module); if(!kept) return kept; }
        // 候補の読込失敗では現行Worldを維持する。停止後のゲーム初期化失敗にrollback保証はしない。
        Stop();
        if(IsRunning() || nativeQuarantined_ || managedQuarantined_) return std::unexpected(Error(ErrorCategory::Framework,273,"Cannot complete the current World shutdown for Level transition."));
        return Start(std::move(next));
    } catch(const std::exception& exception) {
        return std::unexpected(Error(ErrorCategory::Framework,273,std::string("Level transition failed: ")+exception.what()));
    }
}
Result<void> GameRuntime::UpdateWorld(float deltaTime) {
    if(!OnOwnerThread()) return std::unexpected(Error(ErrorCategory::Framework, 45, "GameRuntime Update requires its owner thread."));
    if(updating_ || starting_ || stopping_)
        return std::unexpected(Error(ErrorCategory::Framework, 234, "GameRuntime update cannot be reentered from lifecycle callbacks."));
    if(!std::isfinite(deltaTime) || deltaTime < 0)
        return std::unexpected(Error(ErrorCategory::Framework, 43, "Game delta time must be finite and non-negative."));
    if(!level_ || paused_ || nativePatchPrepared_ || nativePatchFailed_) return {};
    if(IsCSharpSuspended()) return {}; // 復旧可能な停止をユーザーのPauseへ混入させない。
    updating_ = true;
    // callback中にStopを要求されても、SystemSchedulerとManagedのstackが戻るまでWorldを保持する。
    struct UpdateGuard {
        GameRuntime& owner;
        ~UpdateGuard() { owner.updating_ = false; if(owner.pendingStop_) owner.Stop(); }
    } guard{*this};
    try {
        if(nativeModule_) {
            auto result = nativeModule_->Update(deltaTime);
            if(!result) { pendingStop_ = true; return result; }
        }
        for(auto& module : additionalNativeModules_) {
            if(pendingStop_) return {};
            auto updated = module->Update(deltaTime); if(!updated) { pendingStop_ = true; return updated; }
        }
        if(pendingStop_) return {};
        auto result = level_->Update(deltaTime);
        if(result && !pendingStop_ && gameApi_) gameApi_->PublishPhysicsContacts();
        // Native Systemも既存Schedulerから失敗するため、DLLのModule.update成功だけで実行を継続しない。
        if(!result) pendingStop_ = true;
        return result;
    } catch(const std::exception& exception) {
        // Schedulerの実行guardが戻ってからUpdateGuardがStopする。例外中にSystemを破棄しない。
        pendingStop_ = true;
        return std::unexpected(Error(ErrorCategory::Framework, 44, std::string("Game update failed: ") + exception.what()));
    } catch(...) {
        pendingStop_ = true;
        return std::unexpected(Error(ErrorCategory::Framework, 44, "Game update failed with an unknown exception."));
    }
}
Result<void> GameRuntime::ReloadCSharp(const std::filesystem::path& assembly) {
    if(!OnOwnerThread()) return std::unexpected(Error(ErrorCategory::Framework, 45, "Managed Reload requires its Runtime owner thread."));
    if(!level_ || !gameApi_ || !csharpRuntime_ || starting_ || updating_ || stopping_ || nativePatchPrepared_ || nativePatchFailed_)
        return std::unexpected(Error(ErrorCategory::Framework, 235, "Managed Reload requires a frame boundary in a running Managed World."));
    updating_ = true;
    struct ReloadGuard {
        GameRuntime& owner;
        ~ReloadGuard() { owner.updating_ = false; if(owner.pendingStop_) owner.Stop(); }
    } guard{*this};
    try {
        auto result = csharpRuntime_->Reload(assembly, *level_, *gameApi_);
        if(result && managedSettings_) managedSettings_->gameAssembly = assembly;
        return result;
    } catch(const std::exception& exception) {
        return std::unexpected(Error(ErrorCategory::Framework, 235, "Managed Reload: " + std::string(exception.what())));
    } catch(...) {
        return std::unexpected(Error(ErrorCategory::Framework, 235, "Managed Reload failed with an unknown exception."));
    }
}
Result<void> GameRuntime::PrepareNativePatch() {
    if(!OnOwnerThread()) return std::unexpected(Error(ErrorCategory::Framework, 45, "Native patch preparation requires its Runtime owner thread."));
    if(!level_ || !nativeModule_ || !additionalNativeModules_.empty() || !gameApi_ || starting_ || updating_ || stopping_ || nativePatchPrepared_ || nativePatchFailed_ || IsCSharpSuspended() || level_->GetSystemScheduler().IsExecuting() || !GameApiBridge::MatchesWorld(*level_, gameApi_->World()))
        return std::unexpected(Error(ErrorCategory::Framework, 240, "Native patch requires an idle frame boundary and an active compatible Native Module."));
    nativePatchState_ = NativePatchState::CompatibilityError;
    if(!gameApi_->FreezeManagedServices(true)) return std::unexpected(Error(ErrorCategory::Framework, 240, "Native callbacks have not completed."));
    auto prepared = nativeModule_->PreparePatch();
    if(!prepared) { static_cast<void>(gameApi_->FreezeManagedServices(false)); return prepared; }
    nativePatchPrepared_ = true; nativePatchState_ = NativePatchState::Prepared; return {};
}
bool GameRuntime::ValidateNativePatch() noexcept {
    return OnOwnerThread() && nativePatchPrepared_ && level_ && gameApi_ && nativeModule_ &&
        GameApiBridge::MatchesWorld(*level_, gameApi_->World()) && nativeModule_->ValidatePreparedPatch();
}
void GameRuntime::AbandonNativePatch() noexcept {
    if(!OnOwnerThread()) return;
    if(nativeModule_) nativeModule_->AbandonPatch();
    nativePatchPrepared_ = false; nativePatchFailed_ = true; paused_ = true;
    nativePatchState_ = NativePatchState::StateRestoreError;
}
Result<void> GameRuntime::FinishNativePatch(bool applied) {
    if(!OnOwnerThread()) return std::unexpected(Error(ErrorCategory::Framework, 45, "Native patch completion requires its Runtime owner thread."));
    if(!nativePatchPrepared_ || !nativeModule_ || !gameApi_ || starting_ || updating_ || stopping_ || !level_ || !GameApiBridge::MatchesWorld(*level_, gameApi_->World()))
        return std::unexpected(Error(ErrorCategory::Framework, 240, "Native patch completion requires its prepared Runtime."));
    auto result = nativeModule_->FinishPatch(applied);
    nativePatchPrepared_ = false;
    if(result && !gameApi_->FreezeManagedServices(false)) result = std::unexpected(Error(ErrorCategory::Framework, 240, "Cannot resume Native / Managed services after patching."));
    if(!result) {
        nativePatchFailed_ = true; paused_ = true; nativePatchState_ = NativePatchState::StateRestoreError;
        NEXUS_LOG_ERROR("GameRuntime", result.error().GetMessageText());
    } else nativePatchState_ = applied ? NativePatchState::Succeeded : NativePatchState::NotApplied;
    if(pendingStop_) Stop();
    return result;
}
}
