#include "LiveCodingAgent.h"
#include "Foundation/Logging/Logger.h"
#include "Foundation/Reflection/ReinstancingService.h"
#include "Foundation/Utility/String/ConvertString.h"
#include <utility>
#include <cstdlib>
#include <atomic>
#include <thread>
#include <fstream>
#include <iterator>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cwchar>
#include <stdexcept>
#include <mutex>
#include "Runtime/Application/GameRuntime.h"
#include "Runtime/Gameplay/GameApiBridge.h"

#ifndef NEXUS_WITH_LIVEPP
#define NEXUS_WITH_LIVEPP 0
#endif
#ifndef NEXUS_LIVEPP_GAME_MODULE_ONLY
#define NEXUS_LIVEPP_GAME_MODULE_ONLY 0
#endif

#if NEXUS_WITH_LIVEPP
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "API/x64/LPP_API_x64_CPP.h"
#endif

namespace NexusEngine {
    namespace {
        std::atomic<NativeReloadState> reloadState{NativeReloadState::Unavailable}; //< hookからも値だけで通知する
        std::atomic<bool> reloadRequested{false}, reloadBlocked{false}, compileFailed{false}; //< SDK呼出しは所有Threadへ戻す
#if NEXUS_WITH_LIVEPP && NEXUS_LIVEPP_GAME_MODULE_ONLY
        std::recursive_mutex backendMutex; //< 誤Threadからの登録も直列化する。Finish中の同Thread Stopは再入可能
        LiveCodingAgent* activeBackend = nullptr; //< Framework所有。所有Thread以外からアクセスしない
        GameRuntime* registeredRuntime = nullptr; //< RuntimeがStopで必ず解除する非所有参照
        NativeGameModuleSettings registeredSettings; //< Agent生成前ロードを扱う値コピー
        std::thread::id registrationThread;
        std::atomic<bool> moduleRejected{false}; //< pending patch破棄APIを仮定せず、互換性拒否後はProcess再起動を要求する
#endif
    }
#if NEXUS_WITH_LIVEPP && !NEXUS_LIVEPP_GAME_MODULE_ONLY
    // 同期 Agent のフックはフレーム境界の Reload 内で呼び出される。
    void BeforeLiveCodingPatch(lpp::LppHotReloadPrepatchHookId, const wchar_t*, const wchar_t* const*, unsigned int, const wchar_t* const*, unsigned int) {
        NEXUS_LOG_INFO("Live++", "Pre-patch: releasing reflected instances.");
        if(!ReinstancingService::Get().BeforePatch()) {
            // フックの前提が崩れたままパッチを適用すると旧レイアウトへ新コードがアクセスする。
            NEXUS_LOG_CRITICAL("Live++", "Pre-patch hook ran without a prepared snapshot; terminating before unsafe patching.");
            std::_Exit(EXIT_FAILURE);
        }
    }
    void AfterLiveCodingPatch(lpp::LppHotReloadPostpatchHookId, const wchar_t*, const wchar_t* const*, unsigned int, const wchar_t* const*, unsigned int) {
        NEXUS_LOG_INFO("Live++", "Post-patch: restoring reflected instances.");
        ReinstancingService::Get().AfterPatch();
    }
    void LiveCodingCompileError(lpp::LppCompileErrorHookId, const wchar_t*, const wchar_t*, const wchar_t* output) {
        compileFailed.store(true);
        if(output) NEXUS_LOG_ERROR("Live++", ConvertString(std::wstring(output)));
    }
    LPP_HOTRELOAD_PREPATCH_HOOK(BeforeLiveCodingPatch);
    LPP_HOTRELOAD_POSTPATCH_HOOK(AfterLiveCodingPatch);
    LPP_COMPILE_ERROR_HOOK(LiveCodingCompileError);
#endif
    /*-----------------------------------------------------------------------------------------
     * LiveCodingAgent::Impl
     * - SDK Agentと、Module-only時の固定入力・hook結果を所有する
     * - GameRuntime / DLLは所有せず、Disable通知とObserver解除をDLL解放より先に行う
     *---------------------------------------------------------------------------------------*/
    class LiveCodingAgent::Impl final {
    public:
#if NEXUS_WITH_LIVEPP
        lpp::LppSynchronizedAgent agent {};
        std::wstring projectDirectory;
        std::wstring powershell;
        std::string generatorOptions;
        std::string lastError;
#if NEXUS_LIVEPP_GAME_MODULE_ONLY
        std::thread::id thread = std::this_thread::get_id(); //< SDKの接続・更新・解除を直列実行する
        NexusSetNativePatchObserverFn observer = nullptr; //< DLLがロード中の間だけ借用するexport
        std::wstring modulePath; //< SDKの非同期Enable / Disable通知に渡す絶対配置
        std::vector<std::pair<std::filesystem::path, std::string>> inputs; //< Public APIとBuild設定を固定する
        std::filesystem::path sourceDirectory; //< Observer登録中は不変。hookへSource配置を値で渡す
        std::vector<std::pair<std::filesystem::path, std::string>> sourceInputs; //< Loaderと同じheader / 翻訳単位snapshot。Worldを借用しない
        std::atomic<bool> attempting{false}, preSeen{false}, postSeen{false}, hookFailed{false}; //< hook Threadからは値で通知する
        /** \brief 外部SDK依存のないゲームstateを保持するため、固定入力の変更を拒否する */
        static auto Inputs() {
            std::vector<std::pair<std::filesystem::path, std::string>> files;
            uintmax_t total = 0;
            const auto add = [&](const std::filesystem::path& path) {
                if(std::filesystem::is_symlink(path)) throw std::runtime_error("Live++ fixed inputs cannot be symlinks.");
                const auto size = std::filesystem::file_size(path); total += size;
                if(files.size() >= 4096 || size > 2 * 1024 * 1024 || total > 16 * 1024 * 1024) throw std::runtime_error("Live++ fixed input limit exceeded.");
                std::ifstream file(path, std::ios::binary); if(!file) throw std::runtime_error("Cannot read Live++ fixed input.");
                std::string text{std::istreambuf_iterator<char>(file), {}}; if(file.bad()) throw std::runtime_error("Cannot read Live++ fixed input.");
                files.emplace_back(path, std::move(text));
            };
            const auto engine = std::filesystem::path(ConvertString(std::string(NEXUS_PROJECT_DIRECTORY)));
            for(const auto& directory : {engine / "Public", engine / "Build"}) {
                for(const auto& item : std::filesystem::recursive_directory_iterator(directory)) {
                    if(item.is_symlink()) throw std::runtime_error("Live++ fixed inputs cannot be symlinks.");
                    if(item.is_regular_file()) add(item.path());
                }
            }
            if(!registeredSettings.patchProjectDirectory.is_absolute()) throw std::runtime_error("Live++ requires an absolute Game Project root.");
            for(const auto& item : std::filesystem::directory_iterator(registeredSettings.patchProjectDirectory)) {
                const auto extension = item.path().extension();
                if(item.is_regular_file() && (extension == ".props" || extension == ".targets" || extension == ".vcxproj")) add(item.path());
            }
            // hook自体の変更をパッチ許可すると、適用前の検証・適用結果通知を失うため固定する。
            add(registeredSettings.patchSourceDirectory / "NativePatchHooks.cpp");
            std::ranges::sort(files, {}, &std::pair<std::filesystem::path, std::string>::first); return files;
        }
        /** \brief Game DLLのhookから呼ばれる安定Engine entry。ゲーム側へEngine内部pointerを返さない */
        static uint32_t NEXUS_GAME_CALL Notify(void* context, uint32_t event, NexusGameString message) noexcept {
            auto& self = *static_cast<Impl*>(context);
            if(event == NEXUS_NATIVE_PATCH_COMPILE_ERROR || event == NEXUS_NATIVE_PATCH_LINK_ERROR) {
                compileFailed.store(true);
                try { NEXUS_LOG_ERROR("Live++", message.data && message.length && message.length <= 65536 ? std::string(message.data, message.length) : "Native compilation or linking failed; see Live++ Broker output."); } catch(...) {}
                return 1;
            }
            // 実SDKのhookはreceiver Threadからも呼ばれる。ownerの停止・準備完了をatomicで公開し、
            // hookからWorld / Loaderへ再入しない。Observer解除はDLL側mutexで通知完了を待つ。
            if(!self.attempting.load()) {
                self.hookFailed.store(true);
                NEXUS_LOG_CRITICAL("Live++", std::string("Native patch hook requires a prepared operation: ownerThread=") +
                    (self.thread == std::this_thread::get_id() ? "yes" : "no") + ", attempting=" + (self.attempting.load() ? "yes" : "no") +
                    ", event=" + std::to_string(event));
                return 0;
            }
            if(event == NEXUS_NATIVE_PATCH_SOURCE) {
                try {
                    if(!message.data || !message.length || message.length > 65536) return 0;
                    const auto file = std::filesystem::weakly_canonical(std::filesystem::path(ConvertString(std::string(message.data, message.length)))).make_preferred();
                    auto root = std::filesystem::weakly_canonical(self.sourceDirectory).make_preferred().wstring(); root += L'\\';
                    const auto name = file.wstring(), extension = file.extension().wstring();
                    const bool code = _wcsicmp(extension.c_str(), L".cpp") == 0 || _wcsicmp(extension.c_str(), L".cc") == 0 || _wcsicmp(extension.c_str(), L".cxx") == 0;
                    const bool accepted = code && name.size() > root.size() && root.size() <= 32768 &&
                        CompareStringOrdinal(name.c_str(), static_cast<int>(root.size()), root.c_str(), static_cast<int>(root.size()), TRUE) == CSTR_EQUAL &&
                        _wcsicmp(file.filename().c_str(), L"NativePatchHooks.cpp") != 0;
                    if(!accepted) NEXUS_LOG_CRITICAL("Live++", "Native patch source is outside the permitted body-only inputs: " + ConvertString(name));
                    return accepted;
                } catch(...) { self.hookFailed.store(true); return 0; }
            }
            if(event == NEXUS_NATIVE_PATCH_PRE) {
                try {
                    // Runtimeのowner-thread検査を迂回せず、hookでは不変の入力だけを比較する。
                    // Module state契約はownerがSDK呼出し前と適用後に検証する。
                    if(self.preSeen.exchange(true) || Inputs() != self.inputs || GameModuleLoader::ReadPatchSourceSnapshot(self.sourceDirectory) != self.sourceInputs) {
                        self.hookFailed.store(true);
                        NEXUS_LOG_CRITICAL("Live++", "Native pre-patch contract changed after preparation; terminating before applying incompatible code."); return 0;
                    }
                    return 1;
                } catch(...) { self.hookFailed.store(true); return 0; }
            }
            if(event == NEXUS_NATIVE_PATCH_POST && self.preSeen.load()) { self.postSeen.store(true); return 1; }
            self.hookFailed.store(true); return 0;
        }
#endif
        ~Impl() {
            if(lpp::LppIsValidSynchronizedAgent(&agent))
                lpp::LppDestroySynchronizedAgent(&agent);
        }
#endif
    };

    LiveCodingAgent::LiveCodingAgent(bool enabled) {
        if(!enabled) return;
#if NEXUS_WITH_LIVEPP
        auto implementation = std::make_unique<Impl>();
        implementation->projectDirectory = ConvertString(std::string(NEXUS_PROJECT_DIRECTORY));
        wchar_t systemDirectory[MAX_PATH]{};
        if(GetSystemDirectoryW(systemDirectory, MAX_PATH) == 0) throw std::runtime_error("Cannot locate Windows PowerShell");
        implementation->powershell = (std::filesystem::path(systemDirectory) / L"WindowsPowerShell/v1.0/powershell.exe").wstring();
        implementation->generatorOptions = "-NoProfile -ExecutionPolicy Bypass -File \"" + std::string(NEXUS_PROJECT_DIRECTORY) +
            "/Tools/Reflection/Generate-Reflection.ps1\" -ProjectRoot \"" + std::string(NEXUS_PROJECT_DIRECTORY) + "\"";
        auto preferences = lpp::LppCreateDefaultProjectPreferences();
        preferences.hotReload.preBuild.isEnabled = false;
#if NEXUS_LIVEPP_GAME_MODULE_ONLY
        // 2.11.4の実SDKで確認した既定値を明示する。停止中Processのreceiver Threadでmutexを待たせない。
        preferences.hotReload.callCompileHooksForHaltedProcesses = false;
        preferences.hotReload.callLinkHooksForHaltedProcesses = false;
        preferences.hotReload.callHotReloadHooksForHaltedProcesses = false;
#endif
#if !NEXUS_LIVEPP_GAME_MODULE_ONLY
        preferences.hotReload.preBuild.isEnabled = true;
        preferences.hotReload.preBuild.executable = implementation->powershell.c_str();
        preferences.hotReload.preBuild.workingDirectory = implementation->projectDirectory.c_str();
        preferences.hotReload.preBuild.commandLineOptions = implementation->generatorOptions.c_str();
#endif
        implementation->agent = lpp::LppCreateSynchronizedAgentWithPreferences(nullptr, NEXUS_LIVEPP_DIRECTORY, &preferences);
        if(!lpp::LppIsValidSynchronizedAgent(&implementation->agent)) {
            NEXUS_LOG_WARNING("Live++", "Agent could not be loaded. Check the SDK path and Broker license; continuing without live coding.");
            return;
        }
#if NEXUS_LIVEPP_GAME_MODULE_ONLY
        // EXE・Engine・import DLLを有効化しない。既存Reinstancing hookとはprofile単位で分離する。
        std::lock_guard registryLock(backendMutex);
        if(activeBackend) throw std::runtime_error("Only one module-only Live++ Backend may exist in a process.");
        impl_ = std::move(implementation); activeBackend = this;
        try {
            if(registeredRuntime && !AttachGameModule()) throw std::runtime_error("Cannot attach the pending Native Game Module to Live++.");
            reloadState.store(registeredRuntime ? NativeReloadState::Idle : NativeReloadState::Unavailable);
            NEXUS_LOG_INFO("Live++", "Module-only synchronized Backend enabled; Engine / EXE patching is disabled.");
        } catch(...) {
            if(!DetachGameModule()) {
                if(registeredRuntime) registeredRuntime->AbandonNativePatch();
                GameApiBridge::BlockLegacyPatches(); static_cast<void>(impl_.release());
            }
            activeBackend = nullptr; throw;
        }
#else
        implementation->agent.EnableModule(lpp::LppGetCurrentModulePath(), lpp::LPP_MODULES_OPTION_NONE, nullptr, nullptr);
        impl_ = std::move(implementation);
        auto& reinstancing = ReinstancingService::Get();
        const auto project = std::filesystem::path(impl_->projectDirectory);
        reinstancing.SetRecoveryPath(project / L"../Generated/LiveCoding" / (L"Recovery_" + std::to_wstring(GetCurrentProcessId()) + L".nxreload"));
        // 再生成しないホストのヘッダ変更は、パッチ適用前に保守的に拒否する。
        for(const auto& directory : {"Foundation", "Core", "Platform", "Graphics", "Runtime", "Public", "Editor/ImGui", "Editor/Gui/Core"}) {
            for(const auto& entry : std::filesystem::recursive_directory_iterator(project / directory)) {
                const auto relative = entry.path().lexically_relative(project).generic_string();
                if(relative.starts_with("Runtime/Scene/Components/") || relative.starts_with("Runtime/Reflection/Generated/") ||
                   relative.starts_with("Runtime/Reflection/EngineGenerated/")) continue;
                if(entry.is_regular_file() && entry.path().extension() == ".h") reinstancing.ProtectFile(entry.path());
            }
        }
        for(const auto& file : {"Editor/Core/EditorApplication.h", "Editor/Core/EditorSession.h", "Runtime/Scene/ComponentStorage.h",
            "Foundation/Reflection/ReflectionValue.h", "Foundation/Reflection/ReinstancingService.h", "Foundation/Math/Vector3.h", "Foundation/Math/Quaternion.h",
            "Graphics/Model/ModelAssetData.h", "Graphics/Model/ModelInstance.h", "Graphics/Renderer/RenderScene.h", "Editor/Services/EditorSettings.h"}) reinstancing.ProtectFile(project / file);
        reloadState.store(NativeReloadState::Idle);
        NEXUS_LOG_INFO("Live++", "Synchronized live coding and reflected reinstancing enabled. Save source changes and press Ctrl+Alt+F11.");
#endif
#else
        NEXUS_LOG_INFO("Live++", "Live coding is disabled in this build. See Docs/LiveCoding.md for SDK setup.");
#endif
    }

    LiveCodingAgent::~LiveCodingAgent() {
#if NEXUS_WITH_LIVEPP && NEXUS_LIVEPP_GAME_MODULE_ONLY
        // FrameworkがRuntimeより先に終了するStandaloneでも、DLLが存在する間に通知する。
        std::lock_guard registryLock(backendMutex);
        if(activeBackend == this) {
            if(!DetachGameModule()) {
                if(registeredRuntime) registeredRuntime->AbandonNativePatch();
                GameApiBridge::BlockLegacyPatches();
                // DLLのObserver解除を確認できない場合はcontextとSDKもProcess終了まで保持する。
                static_cast<void>(impl_.release());
            }
            activeBackend = nullptr;
        }
#endif
        impl_.reset(); reloadState.store(NativeReloadState::Unavailable); reloadRequested.store(false); reloadBlocked.store(false);
    }

    NativeReloadState LiveCodingAgent::State() noexcept { return reloadState.load(); }
    bool LiveCodingAgent::IsGameModuleBackend() const noexcept { return NEXUS_WITH_LIVEPP && NEXUS_LIVEPP_GAME_MODULE_ONLY; }
    bool LiveCodingAgent::RegisterGameRuntime(GameRuntime& runtime, const NativeGameModuleSettings& settings) noexcept {
#if NEXUS_WITH_LIVEPP && NEXUS_LIVEPP_GAME_MODULE_ONLY
        std::lock_guard registryLock(backendMutex);
        if(moduleRejected.load() || registeredRuntime || !settings.file.is_absolute() || !settings.patchSourceDirectory.is_absolute() || !settings.patchProjectDirectory.is_absolute()) return false;
        try {
            registeredSettings = settings; registrationThread = std::this_thread::get_id(); registeredRuntime = &runtime;
            if(activeBackend && !activeBackend->AttachGameModule()) { registeredRuntime = nullptr; registeredSettings = {}; return false; }
            // Agent生成前はUnavailableのまま保持する。Agentが接続後にだけEditor操作を受け付ける。
            if(activeBackend) reloadState.store(NativeReloadState::Idle);
            return true;
        } catch(...) { registeredRuntime = nullptr; registeredSettings = {}; return false; }
#else
        static_cast<void>(runtime); static_cast<void>(settings); return false;
#endif
    }
    bool LiveCodingAgent::UnregisterGameRuntime(GameRuntime& runtime) noexcept {
#if NEXUS_WITH_LIVEPP && NEXUS_LIVEPP_GAME_MODULE_ONLY
        std::lock_guard registryLock(backendMutex);
        if(registeredRuntime != &runtime) return false;
        const bool detached = registrationThread == std::this_thread::get_id() && (!activeBackend || activeBackend->DetachGameModule());
        // 誤ThreadではSDKを操作しない。破棄されるRuntimeへの参照だけは必ず失効させ、DLLは呼出元がpinする。
        if(!detached) { moduleRejected.store(true); GameApiBridge::BlockLegacyPatches(); }
        registeredRuntime = nullptr; registeredSettings = {};
        reloadRequested.store(false); reloadBlocked.store(false); reloadState.store(NativeReloadState::Unavailable);
        return detached;
#else
        static_cast<void>(runtime); return false;
#endif
    }
    bool LiveCodingAgent::AttachGameModule() {
#if NEXUS_WITH_LIVEPP && NEXUS_LIVEPP_GAME_MODULE_ONLY
        if(moduleRejected.load() || !impl_ || !registeredRuntime || impl_->thread != registrationThread || impl_->observer) return false;
        try {
            auto inputs = Impl::Inputs();
            const auto module = GetModuleHandleW(registeredSettings.file.c_str());
            const auto address = module ? GetProcAddress(module, NEXUS_NATIVE_PATCH_OBSERVER_ENTRY) : nullptr;
            NexusSetNativePatchObserverFn observer = nullptr;
            static_assert(sizeof(observer) == sizeof(address)); std::memcpy(&observer, &address, sizeof(observer));
            if(!observer) return false;
            impl_->modulePath = registeredSettings.file.wstring(); impl_->inputs = std::move(inputs);
            impl_->sourceDirectory = registeredSettings.patchSourceDirectory;
            impl_->sourceInputs = GameModuleLoader::ReadPatchSourceSnapshot(impl_->sourceDirectory);
            const NexusNativePatchObserverV1 context{1, sizeof(NexusNativePatchObserverV1), impl_.get(), Impl::Notify};
            if(observer(&context) != NEXUS_GAME_OK) return false;
            impl_->observer = observer;
            compileFailed.store(false);
            // filter callbackを保持させない。APIは完了結果を返さず、通知順序だけを保証する。
            impl_->agent.EnableModule(impl_->modulePath.c_str(), lpp::LPP_MODULES_OPTION_NONE, nullptr, nullptr);
            return true;
        } catch(...) {
            if(!DetachGameModule()) {
                if(registeredRuntime) registeredRuntime->AbandonNativePatch();
                GameApiBridge::BlockLegacyPatches();
                // 失敗後にDLLがcontextを保持している可能性があればBackendもpinする。
                static_cast<void>(impl_.release());
            }
            return false;
        }
#else
        return false;
#endif
    }
    bool LiveCodingAgent::DetachGameModule() noexcept {
#if NEXUS_WITH_LIVEPP && NEXUS_LIVEPP_GAME_MODULE_ONLY
        if(!impl_ || !impl_->observer) return true;
        if(impl_->thread != std::this_thread::get_id() || impl_->attempting.load()) return false;
        try {
            // DLLがロード中の間に、Enableと同じoptionでDisable通知する。完了確認APIとは扱わない。
            impl_->agent.DisableModule(impl_->modulePath.c_str(), lpp::LPP_MODULES_OPTION_NONE, nullptr, nullptr);
            if(impl_->observer(nullptr) != NEXUS_GAME_OK) return false;
            impl_->observer = nullptr; impl_->modulePath.clear(); impl_->inputs.clear();
            impl_->sourceDirectory.clear(); impl_->sourceInputs.clear(); return true;
        } catch(...) { return false; }
#else
        return true;
#endif
    }
    void LiveCodingAgent::ReloadGameModule() {
#if NEXUS_WITH_LIVEPP && NEXUS_LIVEPP_GAME_MODULE_ONLY
        std::lock_guard registryLock(backendMutex);
        if(moduleRejected.load() || !impl_ || !impl_->observer || !registeredRuntime || impl_->thread != std::this_thread::get_id()) { RejectReload(); return; }
        auto* runtime = registeredRuntime;
        auto* level = runtime->GetLevel();
        if(!level || !GameApiBridge::BeginGameModulePatch(*level)) { RejectReload(); return; }
        struct Lease { ~Lease() { GameApiBridge::EndLegacyPatch(); } } lease;
        try {
            if(Impl::Inputs() != impl_->inputs) { RejectReload(); return; }
        } catch(...) { RejectReload(); return; }
        auto prepared = runtime->PrepareNativePatch();
        if(!prepared) { NEXUS_LOG_ERROR("Live++", prepared.error().GetMessageText()); RejectReload(); return; }
        // SDK callbackへRuntime pointerを使わせず、所有ThreadでWorld / Module契約を確定する。
        if(!runtime->ValidateNativePatch()) {
            const auto cancelled = runtime->FinishNativePatch(false);
            if(!cancelled) runtime->AbandonNativePatch();
            RejectReload(); return;
        }
        impl_->preSeen.store(false); impl_->postSeen.store(false); impl_->hookFailed.store(false); impl_->attempting.store(true);
        reloadState.store(NativeReloadState::Reloading);
        try {
            // Engine / Editorのstackは固定のまま、登録済みGame DLLだけを同期パッチする。
            impl_->agent.Reload(lpp::LPP_RELOAD_BEHAVIOUR_WAIT_UNTIL_CHANGES_ARE_APPLIED);
            impl_->attempting.store(false);
            const bool applied = impl_->postSeen.load();
            if(impl_->hookFailed.load() || impl_->preSeen.load() != applied) {
                runtime->AbandonNativePatch(); reloadState.store(NativeReloadState::StateRestoreError); reloadBlocked.store(true);
                NEXUS_LOG_ERROR("Live++", "Native patch result is uncertain; DLL / state pinned. Restart the host."); return;
            }
            const auto result = runtime->FinishNativePatch(applied);
            if(!result) {
                NEXUS_LOG_ERROR("Live++", result.error().GetMessageText()); reloadState.store(NativeReloadState::StateRestoreError); reloadBlocked.store(true);
            } else {
                reloadState.store(applied ? NativeReloadState::Succeeded : compileFailed.load() ? NativeReloadState::CompileError : NativeReloadState::NoChanges);
                NEXUS_LOG_INFO("Live++", applied ? "Game DLL patched; Native API bindings refreshed." : "No Game DLL patch applied; current state retained.");
            }
            compileFailed.store(false);
        } catch(...) {
            impl_->attempting.store(false); runtime->AbandonNativePatch(); reloadState.store(NativeReloadState::StateRestoreError); reloadBlocked.store(true);
            NEXUS_LOG_ERROR("Live++", "Native Backend did not complete safely; DLL / state pinned. Restart the host.");
        }
#endif
    }
    bool LiveCodingAgent::RequestReload() noexcept {
#if NEXUS_WITH_LIVEPP && NEXUS_LIVEPP_GAME_MODULE_ONLY
        if(moduleRejected.load()) return false;
#endif
        auto state = reloadState.load();
        do {
            if(state == NativeReloadState::Unavailable || state == NativeReloadState::Queued || state == NativeReloadState::Reloading) return false;
        } while(!reloadState.compare_exchange_weak(state, NativeReloadState::Queued));
        reloadBlocked.store(false); reloadRequested.store(true); return true;
    }
    void LiveCodingAgent::RejectReload() {
#if NEXUS_WITH_LIVEPP && NEXUS_LIVEPP_GAME_MODULE_ONLY
        moduleRejected.store(true);
#endif
        if(!reloadBlocked.exchange(true)) NEXUS_LOG_ERROR("Live++", IsGameModuleBackend()
            ? "Compatibility Error: module-only reload requires one idle World, unchanged fixed inputs and a valid Runtime contract. Pending patch disposal is not guaranteed; rebuild and restart the host."
            : "Compatibility Error: stop Native / Managed sessions, then explicitly request Reload C++ again. Failed shutdown requires a process restart. Legacy EXE patching cannot migrate Game DLL references.");
        reloadState.store(NativeReloadState::CompatibilityError);
    }

    bool LiveCodingAgent::WantsReload() {
#if NEXUS_WITH_LIVEPP
#if NEXUS_LIVEPP_GAME_MODULE_ONLY
        std::lock_guard registryLock(backendMutex);
        if(moduleRejected.load() || !registeredRuntime) return false;
#endif
        if(impl_ && reloadRequested.exchange(false)) ScheduleReload();
        if(reloadBlocked.load()) return false;
        // pre-patch フックはパッチ読込より前に走るため、コンパイル開始前から同期する。
        return impl_ && impl_->agent.WantsReload(lpp::LPP_RELOAD_OPTION_SYNCHRONIZE_WITH_COMPILATION_AND_RELOAD);
#else
        return false;
#endif
    }

    void LiveCodingAgent::ScheduleReload() {
#if NEXUS_WITH_LIVEPP
        if(impl_) { reloadBlocked.store(false); impl_->agent.ScheduleReload(); }
#endif
    }

    void LiveCodingAgent::Reload() {
        if(IsGameModuleBackend()) { ReloadGameModule(); return; }
#if NEXUS_WITH_LIVEPP
        if(impl_) {
            reloadState.store(NativeReloadState::Reloading); compileFailed.store(false);
            auto& reinstancing = ReinstancingService::Get();
            if(!reinstancing.Prepare()) {
                reloadState.store(NativeReloadState::CompatibilityError);
                reloadBlocked.store(true); // 条件変更だけでpendingパッチを自動適用せず、明示再要求を待つ。
                if(impl_->lastError != reinstancing.Error()) {
                    impl_->lastError = reinstancing.Error(); NEXUS_LOG_ERROR("Live++", impl_->lastError);
                }
                return;
            }
            impl_->lastError.clear();
            const auto generation = reinstancing.Generation();
            NEXUS_LOG_INFO("Live++", "Snapshot prepared; applying synchronized reload.");
            impl_->agent.Reload(lpp::LPP_RELOAD_BEHAVIOUR_WAIT_UNTIL_CHANGES_ARE_APPLIED);
            try { reinstancing.FinishAttempt(); }
            catch(...) { reloadState.store(NativeReloadState::StateRestoreError); throw; }
            reloadState.store(compileFailed.load() ? NativeReloadState::CompileError : reinstancing.Generation() != generation ? NativeReloadState::Succeeded : NativeReloadState::NoChanges);
            if(reinstancing.Generation() != generation) NEXUS_LOG_SUCCESS("Live++", "Reload completed; reflected instances are ready.");
            else NEXUS_LOG_INFO("Live++", "No patch was applied; existing reflected instances retained.");
        }
#endif
    }
}
