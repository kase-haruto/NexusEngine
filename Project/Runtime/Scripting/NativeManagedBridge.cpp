#include "NativeManagedBridge.h"
#include "DotNetHost.h"
#include "MonoHost.h"
#include "Foundation/Logging/Logger.h"
#include "Runtime/Project/ProjectJson.h"
#include <cstring>
#include <string>
#include <limits>

namespace NexusEngine {
namespace {
/** \brief 任意feature exportを値で確認する。pointerを保存せず、Domain解放後に残さない */
bool ScriptAttachmentFeature(DotNetHost& host, const std::filesystem::path& assembly) {
    const auto address = host.Resolve(assembly, L"SupportsScriptAttachments"); if(!address) return false;
    using Feature = NexusGameStatus(NEXUS_GAME_CALL*)(uint32_t);
    Feature feature = nullptr; static_assert(sizeof(feature) == sizeof(void*)); std::memcpy(&feature, &*address, sizeof(feature));
    try { return feature && feature(1) == NEXUS_GAME_OK; } catch(...) { return false; }
}
/** 旧Bootstrapへ未定義のTick modeを渡さず、明示対応している場合だけ固定callbackを使う。 */
bool FixedUpdateFeature(DotNetHost& host, const std::filesystem::path& assembly) {
    const auto address = host.Resolve(assembly, L"SupportsFixedUpdates");
    if(!address) {
        return false;
    }
    using Feature = NexusGameStatus(NEXUS_GAME_CALL*)(uint32_t);
    Feature feature = nullptr;
    static_assert(sizeof(feature) == sizeof(void*));
    std::memcpy(&feature, &*address, sizeof(feature));
    try {
        return feature && feature(1) == NEXUS_GAME_OK;
    } catch(...) {
        return false;
    }
}
}
NativeManagedBridge::~NativeManagedBridge() { static_cast<void>(Stop()); }
Result<void> NativeManagedBridge::StartBoundGameplay(DotNetHost& host, const std::filesystem::path& assembly, const NexusManagedGameplayContextV5& context) {
    if(session_ || start_ || stop_ || gameplayStart_ || tick_ || reload_)
        return std::unexpected(Error(ErrorCategory::Framework, 238, "Managed bridge is already bound."));
    host_ = &host; monoSession_ = host.IsMono(); bootstrap_ = assembly;
    if(monoSession_) {
        if(!context.gameplay.gameplay.gameAssembly.data || context.gameplay.gameplay.gameAssembly.length > 131072)
            return std::unexpected(Error(ErrorCategory::Framework, 239, "Mono requires a valid bounded Game Assembly path."));
        monoContext_ = context;
        monoGameAssembly_.assign(context.gameplay.gameplay.gameAssembly.data, context.gameplay.gameplay.gameAssembly.length);
        monoContext_.gameplay.gameplay.gameAssembly = {};
    }
    auto start = host.Resolve(assembly, L"StartBoundGameplay"); if(!start) return std::unexpected(start.error());
    auto stop = host.Resolve(assembly, L"Stop"); if(!stop) return std::unexpected(stop.error());
    auto tick = host.Resolve(assembly, L"Tick"); if(!tick) return std::unexpected(tick.error());
    auto reload = host.IsMono() ? Result<void*>{nullptr} : host.Resolve(assembly, L"Reload"); if(!reload) return std::unexpected(reload.error());
    NexusManagedBoundStartFn entry = nullptr; static_assert(sizeof(entry) == sizeof(void*));
    std::memcpy(&entry, &*start, sizeof(entry)); std::memcpy(&stop_, &*stop, sizeof(stop_));
    std::memcpy(&tick_, &*tick, sizeof(tick_)); std::memcpy(&reload_, &*reload, sizeof(reload_));
    thread_ = std::this_thread::get_id();
    try {
        scriptAttachments_ = ScriptAttachmentFeature(host, assembly);
        fixedUpdates_ = FixedUpdateFeature(host, assembly);
        const auto status = entry(&context, &session_);
        if(status == NEXUS_GAME_OK && session_) return {};
        return std::unexpected(Error(ErrorCategory::Framework, 238, "Managed Binding Start failed (" + std::to_string(status) + ")."));
    } catch(...) { return std::unexpected(Error(ErrorCategory::Framework, 238, "Managed Binding Start violated the exception contract.")); }
}
Result<void> NativeManagedBridge::Start(DotNetHost& host, const std::filesystem::path& assembly, const NexusManagedBootstrapContextV1& context) {
    if(session_ || start_ || stop_ || gameplayStart_ || tick_) return std::unexpected(Error(ErrorCategory::Framework, 231, "Managed bridge is already bound."));
    host_ = &host; monoSession_ = host.IsMono(); bootstrap_ = assembly;
    auto start = host.Resolve(assembly, L"Start"); if(!start) return std::unexpected(start.error());
    auto stop = host.Resolve(assembly, L"Stop"); if(!stop) return std::unexpected(stop.error());
    static_assert(sizeof(start_) == sizeof(void*) && sizeof(stop_) == sizeof(void*));
    std::memcpy(&start_, &*start, sizeof(start_));
    std::memcpy(&stop_, &*stop, sizeof(stop_));
    thread_ = std::this_thread::get_id();
    try {
        const auto status = start_(&context, &session_);
        if(status == NEXUS_GAME_OK && session_) return {};
        return std::unexpected(Error(ErrorCategory::Framework, 231, "Managed bootstrap Start failed (" + std::to_string(status) + ")."));
    } catch(...) { return std::unexpected(Error(ErrorCategory::Framework, 231, "Managed Start violated the no-exception ABI contract.")); }
}

bool NativeManagedBridge::Stop() noexcept {
    bool stopped = !cleanupFailed_;
    if(session_) {
        stopped = stopped && thread_ == std::this_thread::get_id() && stop_;
        if(stopped && monoSession_) stopped = host_ && host_->MonoBackend() && host_->MonoBackend()->Enter().has_value();
        if(stopped) {
            try { stopped = stop_(session_) == NEXUS_GAME_OK; }
            catch(...) { stopped = false; }
        }
    }
    // 失敗時にもNative側にManaged callbackを残さない。World tokenは上位が失効させる。
    session_ = 0; start_ = nullptr; stop_ = nullptr; gameplayStart_ = nullptr; tick_ = nullptr; reload_ = nullptr; suspended_ = false; scriptAttachments_ = false; fixedUpdates_ = false;
    host_ = nullptr; bootstrap_.clear(); monoGameAssembly_.clear(); monoContext_ = {}; monoSession_ = false; monoCommitted_ = false;
    if(!stopped) NEXUS_LOG_ERROR("CSharpRuntime", "Managed bootstrap did not stop cleanly. Restart the host process.");
    return stopped;
}
Result<void> NativeManagedBridge::StartGameplay(DotNetHost& host, const std::filesystem::path& assembly, const NexusManagedGameplayContextV2& context) {
    if(session_ || start_ || stop_ || gameplayStart_ || tick_)
        return std::unexpected(Error(ErrorCategory::Framework, 233, "Managed bridge is already bound."));
    host_ = &host; monoSession_ = host.IsMono(); bootstrap_ = assembly;
    auto start = host.Resolve(assembly, L"StartGameplay"); if(!start) return std::unexpected(start.error());
    auto stop = host.Resolve(assembly, L"Stop"); if(!stop) return std::unexpected(stop.error());
    auto tick = host.Resolve(assembly, L"Tick"); if(!tick) return std::unexpected(tick.error());
    static_assert(sizeof(gameplayStart_) == sizeof(void*) && sizeof(tick_) == sizeof(void*));
    std::memcpy(&gameplayStart_, &*start, sizeof(gameplayStart_));
    std::memcpy(&stop_, &*stop, sizeof(stop_)); std::memcpy(&tick_, &*tick, sizeof(tick_));
    thread_ = std::this_thread::get_id();
    try {
        const auto status = gameplayStart_(&context, &session_);
        if(status == NEXUS_GAME_OK && session_) return {};
        return std::unexpected(Error(ErrorCategory::Framework, 233, "Managed Gameplay Start failed (" + std::to_string(status) + ")."));
    } catch(...) { return std::unexpected(Error(ErrorCategory::Framework, 233, "Managed Gameplay Start violated the exception contract.")); }
}
Result<void> NativeManagedBridge::Tick(std::span<NexusScriptBindingV1> bindings, float deltaTime, bool initializeOnly, bool fixedOnly) {
    if(fixedOnly && !fixedUpdates_) {
        return {};
    }
    if(!session_ || !tick_ || suspended_ || thread_ != std::this_thread::get_id() || bindings.size() > (std::numeric_limits<uint32_t>::max)())
        return std::unexpected(Error(ErrorCategory::Framework, 233, "Managed Tick requires an active Gameplay session on its owner thread."));
    if(monoSession_) {
        if(!host_ || !host_->MonoBackend()) return std::unexpected(Error(ErrorCategory::Framework, 239, "Mono Host was stopped before the Bridge."));
        auto entered = host_->MonoBackend()->Enter(); if(!entered) return entered;
    }
    try {
        const auto status = tick_(session_, bindings.data(), static_cast<uint32_t>(bindings.size()), deltaTime, initializeOnly ? 1u : fixedOnly ? 2u : 0u);
        if(status == NEXUS_GAME_OK) return {};
        return std::unexpected(Error(ErrorCategory::Framework, 233, "Managed Tick failed (" + std::to_string(status) + ")."));
    } catch(...) { return std::unexpected(Error(ErrorCategory::Framework, 233, "Managed Tick violated the exception contract.")); }
}
Result<void> NativeManagedBridge::StartReflectedGameplay(DotNetHost& host, const std::filesystem::path& assembly, const NexusManagedGameplayContextV3& context) {
    if(session_ || start_ || stop_ || gameplayStart_ || tick_)
        return std::unexpected(Error(ErrorCategory::Framework, 234, "Managed bridge is already bound."));
    host_ = &host; monoSession_ = host.IsMono(); bootstrap_ = assembly;
    auto start = host.Resolve(assembly, L"StartReflectedGameplay"); if(!start) return std::unexpected(start.error());
    auto reload = host.IsMono() ? Result<void*>{nullptr} : host.Resolve(assembly, L"Reload"); if(!reload) return std::unexpected(reload.error());
    auto stop = host.Resolve(assembly, L"Stop"); if(!stop) return std::unexpected(stop.error());
    auto tick = host.Resolve(assembly, L"Tick"); if(!tick) return std::unexpected(tick.error());
    NexusManagedReflectedStartFn entry = nullptr;
    static_assert(sizeof(entry) == sizeof(void*));
    std::memcpy(&entry, &*start, sizeof(entry));
    static_assert(sizeof(reload_) == sizeof(void*));
    std::memcpy(&reload_, &*reload, sizeof(reload_));
    std::memcpy(&stop_, &*stop, sizeof(stop_)); std::memcpy(&tick_, &*tick, sizeof(tick_));
    thread_ = std::this_thread::get_id();
    try {
        const auto status = entry(&context, &session_);
        if(status == NEXUS_GAME_OK && session_) return {};
        return std::unexpected(Error(ErrorCategory::Framework, 234, "Managed Reflected Gameplay Start failed (" + std::to_string(status) + ")."));
    } catch(...) { return std::unexpected(Error(ErrorCategory::Framework, 234, "Managed Reflected Gameplay Start violated the exception contract.")); }
}
Result<void> NativeManagedBridge::Reload(uint32_t operation, const std::filesystem::path& assembly) {
    if(monoSession_) return ReloadMono(operation, assembly);
    if(!session_ || !reload_ || operation > 3 || thread_ != std::this_thread::get_id())
        return std::unexpected(Error(ErrorCategory::Framework, 235, "Reload requires an active Property-enabled Gameplay session on its owner thread."));
    try {
        const auto path = ProjectJson::Utf8(assembly);
        if(path.size() > 131072) return std::unexpected(Error(ErrorCategory::Framework, 235, "Reload assembly path is too long."));
        uint32_t suspended = suspended_ ? 1u : 0u;
        const auto status = reload_(session_, operation, {path.data(), static_cast<uint32_t>(path.size())}, &suspended);
        suspended_ = suspended != 0;
        if(status == NEXUS_GAME_OK) return {};
        return std::unexpected(Error(ErrorCategory::Framework, 235, "Managed Reload failed at stage " + std::to_string(operation) + " (" + std::to_string(status) + "). See Managed log."));
    } catch(...) { return std::unexpected(Error(ErrorCategory::Framework, 235, "Managed Reload violated the exception contract.")); }
}
Result<void> NativeManagedBridge::ReloadMono(uint32_t operation, const std::filesystem::path& assembly) {
    const auto failed = [](std::string message) { return std::unexpected(Error(ErrorCategory::Framework, 239, "Mono reload: " + std::move(message))); };
    if(!host_ || !host_->MonoBackend() || thread_ != std::this_thread::get_id() || operation > 3 || cleanupFailed_ || monoContext_.gameplay.gameplay.abiVersion != 5)
        return failed("Requires a V5 gameplay session and its live owner-thread Host.");
    auto& backend = *host_->MonoBackend();
    if(operation == 0) {
        if(monoCommitted_ || !assembly.is_absolute() || !std::filesystem::is_regular_file(assembly)) return failed("Invalid candidate or unfinished activation.");
        auto prepared = backend.PrepareReload(session_, assembly);
        if(!prepared && backend.IsQuarantined()) { suspended_ = true; cleanupFailed_ = true; }
        return prepared;
    }
    if(operation == 2) {
        if(monoCommitted_) {
            // Commit後は旧Domainへ戻せない。候補sessionを停止し、Native側のWorld復元を待つ。
            if(session_) {
                auto retiring = backend.BeginRetirement(session_);
                if(!retiring || !stop_ || stop_(session_) != NEXUS_GAME_OK) cleanupFailed_ = true;
            }
            session_ = 0; start_ = nullptr; stop_ = nullptr; gameplayStart_ = nullptr; tick_ = nullptr; reload_ = nullptr;
            suspended_ = true; monoCommitted_ = false;
        }
        backend.CancelPrepared();
        return cleanupFailed_ ? failed("Candidate cleanup failed; restart the host.") : Result<void>{};
    }
    if(operation == 3) {
        if(!monoCommitted_ || !session_) return failed("There is no initialized candidate session.");
        auto completed = backend.MarkActivation(session_, false); if(!completed) return completed;
        backend.CancelPrepared(); monoCommitted_ = false; suspended_ = false;
        return {};
    }
    if(backend.PreparedAssembly().empty()) return failed("Prepare must succeed before Commit.");
    auto retiring = backend.BeginRetirement(session_); if(!retiring) return retiring;
    monoCommitted_ = true; suspended_ = true;
    if(session_ && (!stop_ || stop_(session_) != NEXUS_GAME_OK)) {
        session_ = 0; cleanupFailed_ = true; return failed("Old Script lifecycle cleanup failed; restart the host.");
    }
    // 解放したDomainのthunkを再利用しない。次のDomain生成より先に全callback参照を切る。
    session_ = 0; start_ = nullptr; stop_ = nullptr; gameplayStart_ = nullptr; tick_ = nullptr; reload_ = nullptr;
    auto activated = backend.ActivatePrepared(); if(!activated) return activated;
    const auto candidate = ProjectJson::Utf8(backend.PreparedAssembly());
    auto context = monoContext_;
    context.gameplay.gameplay.gameAssembly = {candidate.data(), static_cast<uint32_t>(candidate.size())};
    const auto bootstrap = bootstrap_; auto* host = host_;
    auto started = StartBoundGameplay(*host, bootstrap, context);
    if(!started) return started;
    auto pending = backend.MarkActivation(session_, true); if(!pending) return pending;
    // Initialize専用Tickだけを許可し、通常更新はComplete成功まで上位Runtimeで停止する。
    suspended_ = false;
    return {};
}
Result<void> NativeManagedBridge::StartHybridGameplay(DotNetHost& host, const std::filesystem::path& assembly, const NexusManagedGameplayContextV4& context) {
    if(session_ || start_ || stop_ || gameplayStart_ || tick_ || reload_)
        return std::unexpected(Error(ErrorCategory::Framework, 236, "Managed bridge is already bound."));
    host_ = &host; monoSession_ = host.IsMono(); bootstrap_ = assembly;
    auto start = host.Resolve(assembly, L"StartHybridGameplay"); if(!start) return std::unexpected(start.error());
    auto stop = host.Resolve(assembly, L"Stop"); if(!stop) return std::unexpected(stop.error());
    auto tick = host.Resolve(assembly, L"Tick"); if(!tick) return std::unexpected(tick.error());
    auto reload = host.IsMono() ? Result<void*>{nullptr} : host.Resolve(assembly, L"Reload"); if(!reload) return std::unexpected(reload.error());
    NexusManagedHybridStartFn entry = nullptr; static_assert(sizeof(entry) == sizeof(void*));
    std::memcpy(&entry, &*start, sizeof(entry)); std::memcpy(&stop_, &*stop, sizeof(stop_));
    std::memcpy(&tick_, &*tick, sizeof(tick_)); std::memcpy(&reload_, &*reload, sizeof(reload_));
    thread_ = std::this_thread::get_id();
    try {
        const auto status = entry(&context, &session_);
        if(status == NEXUS_GAME_OK && session_) return {};
        return std::unexpected(Error(ErrorCategory::Framework, 236, "Managed Hybrid Start failed (" + std::to_string(status) + ")."));
    } catch(...) { return std::unexpected(Error(ErrorCategory::Framework, 236, "Managed Hybrid Start violated the exception contract.")); }
}
}
