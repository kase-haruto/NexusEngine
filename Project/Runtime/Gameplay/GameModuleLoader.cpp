#include "GameModuleLoader.h"
#include "GameApiBridge.h"
#include "NativeGameSystemScope.h"
#include "Public/Nexus/GameComponentMetadata.h"
#include "Runtime/Project/ProjectJson.h"
#include "Foundation/Logging/Logger.h"
#include <Windows.h>
#include <cmath>
#include <cstring>
#include <string_view>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <iterator>
#include <stdexcept>

namespace NexusEngine {
namespace {
Error Failure(const std::string& path, const std::string& operation) {
    return Error(ErrorCategory::Framework, 220, "Native Game Module " + path + ": " + operation);
}
bool Match(NexusGameString actual, const std::string& expected) noexcept {
    return actual.data && actual.length == expected.size() && std::string_view(actual.data, actual.length) == expected;
}
/** \brief exportの表現だけをコピーする。呼出し元がABI・sizeを検証してから使用する */
template<class Function> Function Entry(void* module, const char* name) noexcept {
    const auto address = GetProcAddress(static_cast<HMODULE>(module), name); Function function = nullptr;
    static_assert(sizeof(function) == sizeof(address)); std::memcpy(&function, &address, sizeof(function)); return function;
}
/** \brief inspection時と実ロード・パッチ後の宣言を一致させ、既存値へ別schemaを適用しない */
bool SameComponentMetadata(void* module, const NativeGameModuleSettings& settings) noexcept {
    // DLL由来の例外も、LoaderがDLLを解放する前にここで破棄する。
    try {
    if(!settings.componentMetadataChecked) return true;
    const auto get = Entry<NexusGetGameComponentMetadataFn>(module, NEXUS_GAME_COMPONENT_METADATA_ENTRY);
    if(!get) return settings.componentMetadata.empty();
    NexusGameString value{};
    if(get(1, &value) != NEXUS_GAME_OK || !value.data || !value.length || value.length > 1024 * 1024) return false;
    const auto json = ProjectJson::Parse(std::string_view(value.data, value.length));
    return json && json->dump() == settings.componentMetadata;
    } catch(...) { return false; }
}
/** \brief 永続stateを置くヘッダーと翻訳単位の集合を固定する。CPP内layout変更の自動判定は行わない */
std::vector<std::pair<std::filesystem::path, std::string>> PatchSources(const std::filesystem::path& root) {
    if(!root.is_absolute() || !std::filesystem::is_directory(root)) throw std::runtime_error("Patching requires an existing absolute Native source directory.");
    std::vector<std::pair<std::filesystem::path, std::string>> result; uintmax_t total = 0;
    for(const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if(entry.is_symlink()) throw std::runtime_error("Native patch source must not contain symlinks.");
        if(!entry.is_regular_file()) continue;
        auto extension = entry.path().extension().string();
        std::ranges::transform(extension, extension.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        const bool header = extension == ".h" || extension == ".hpp" || extension == ".inl";
        const bool code = extension == ".cpp" || extension == ".cc" || extension == ".cxx";
        if(!header && !code) continue;
        if(result.size() >= 2048) throw std::runtime_error("Native patch source file limit exceeded.");
        std::string text;
        if(header) {
            const auto bytes = entry.file_size(); total += bytes;
            if(bytes > 2 * 1024 * 1024 || total > 16 * 1024 * 1024) throw std::runtime_error("Native patch header size limit exceeded.");
            std::ifstream file(entry.path(), std::ios::binary); if(!file) throw std::runtime_error("Cannot read Native patch header.");
            text.assign(std::istreambuf_iterator<char>(file), {}); if(file.bad()) throw std::runtime_error("Cannot read Native patch header.");
        }
        result.emplace_back(entry.path().lexically_relative(root), std::move(text));
    }
    if(result.empty()) throw std::runtime_error("Native patch source is empty.");
    std::ranges::sort(result, {}, &std::pair<std::filesystem::path, std::string>::first); return result;
}
bool ValidPatchContract(const NexusNativePatchContractV1& contract) noexcept {
    return contract.abiVersion == 1 && contract.structSize == sizeof(contract) && contract.stateSchema && contract.moduleSize && contract.moduleAlignment &&
        (contract.moduleAlignment & (contract.moduleAlignment - 1)) == 0 && contract.moduleSize % contract.moduleAlignment == 0 && contract.systemsLayout;
}
bool SamePatchContract(const NexusNativePatchContractV1& left, const NexusNativePatchContractV1& right) noexcept {
    return ValidPatchContract(right) && left.stateSchema == right.stateSchema && left.moduleSize == right.moduleSize && left.moduleAlignment == right.moduleAlignment && left.systemsLayout == right.systemsLayout;
}
}
/*-----------------------------------------------------------------------------------------
 * GameModuleLoader::OperationGuard
 * - DLL exportやLogger callbackからのStopを外側の操作終了まで遅延する
 * - Engine側のstackへ戻る前にDLLのstateやコードを解放させない
 *---------------------------------------------------------------------------------------*/
struct GameModuleLoader::OperationGuard {
    GameModuleLoader& owner; //< callback中も呼出元がLoaderを所有し、guardより先に破棄しない
    explicit OperationGuard(GameModuleLoader& loader) noexcept : owner(loader) { owner.operationActive_ = true; }
    OperationGuard(const OperationGuard&) = delete;
    OperationGuard& operator=(const OperationGuard&) = delete;
    ~OperationGuard() noexcept {
        owner.operationActive_ = false;
        if(owner.stopRequested_) { owner.stopRequested_ = false; owner.Stop(); }
    }
};
GameModuleLoader::GameModuleLoader() = default;
GameModuleLoader::~GameModuleLoader() { Stop(); }
std::vector<std::pair<std::filesystem::path, std::string>> GameModuleLoader::ReadPatchSourceSnapshot(const std::filesystem::path& root) {
    return PatchSources(root);
}

Result<void> GameModuleLoader::Start(const NativeGameModuleSettings& settings, const NexusGameModuleContextV1& context, Level* level) {
    if(operationActive_ || stopping_ || moduleHandle_ || quarantined_ || settings.file.empty() || !settings.file.is_absolute() || settings.engineId.empty() ||
       settings.engineVersion.empty() || settings.projectId.empty() || settings.moduleId.empty())
        return std::unexpected(Failure(path_, "requires a stopped loader, an absolute DLL path and explicit identity."));
    path_ = ProjectJson::Utf8(settings.file);
    identity_ = settings;
    patchPrepared_ = false; patchFailed_ = false; unsafePatch_ = false; patchContract_ = {}; patchSources_.clear();
    if(context.abiVersion != NEXUS_GAME_ABI_VERSION || context.structSize != sizeof(context) || !context.world ||
       context.api.abiVersion != NEXUS_GAME_ABI_VERSION || context.api.structSize != sizeof(context.api) ||
       !context.api.createEntity || !context.api.destroyEntity || !context.api.isEntityAlive || !context.api.entities ||
       !context.api.getLocalTransform || !context.api.setLocalTransform || !context.api.log)
        return std::unexpected(Failure(path_, "invalid Game API context."));
    // createが既存Worldへ副作用を出す前に、Systemの所有LevelとWorldの対応を検査する。
    if(level && !GameApiBridge::MatchesWorld(*level, context.world)) return std::unexpected(Failure(path_, "Level does not own the supplied World on this thread."));
    thread_ = std::this_thread::get_id();
    OperationGuard operation{*this};
    try {
        // CWDのDLL探索に依存させず、解決済みDLLのDirectoryとWindows既定配置だけを使用する。
        moduleHandle_ = LoadLibraryExW(settings.file.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if(!moduleHandle_) return std::unexpected(Failure(path_, "LoadLibraryExW failed (" + std::to_string(GetLastError()) + ")."));
        if(stopRequested_) return std::unexpected(Failure(path_, "start cancelled during DLL loading."));
        // FARPROCはWindowsのgeneric関数型。公開ABIの型へ一度だけ変換する。
        const auto address = GetProcAddress(static_cast<HMODULE>(moduleHandle_), NEXUS_GAME_MODULE_ENTRY);
        NexusGetGameModuleFn entry = nullptr;
        static_assert(sizeof(entry) == sizeof(address));
        // 関数型間castのMSVC警告を避け、同じWindows x64表現の関数ポインタだけをコピーする。
        std::memcpy(&entry, &address, sizeof(entry));
        if(!entry) { Stop(); return std::unexpected(Failure(path_, "missing NexusGetGameModule export.")); }
        const auto status = entry(NEXUS_GAME_ABI_VERSION, &module_);
        if(stopRequested_) return std::unexpected(Failure(path_, "start cancelled during metadata callback."));
        if(status != NEXUS_GAME_OK || module_.abiVersion != NEXUS_GAME_ABI_VERSION || module_.structSize != sizeof(module_) ||
           !module_.create || !module_.update || !module_.destroy ||
           !Match(module_.engineId, settings.engineId) || !Match(module_.engineVersion, settings.engineVersion) ||
           !Match(module_.projectId, settings.projectId) || !Match(module_.moduleId, settings.moduleId)) {
            Stop(); return std::unexpected(Failure(path_, "incompatible ABI / Engine / Project / Module identity."));
        }
        const auto systemAddress = GetProcAddress(static_cast<HMODULE>(moduleHandle_), NEXUS_NATIVE_SYSTEMS_ENTRY);
        if(!SameComponentMetadata(moduleHandle_, settings)) { Stop(); return std::unexpected(Failure(path_, "Component metadata changed between inspection and loading.")); }
        const auto bindingAddress = GetProcAddress(static_cast<HMODULE>(moduleHandle_), NEXUS_NATIVE_SYSTEMS_ENTRY_V2);
        if((systemAddress || bindingAddress) && !level) { Stop(); return std::unexpected(Failure(path_, "Native Systems require an owning Level / Scheduler.")); }
        if(!settings.patchSourceDirectory.empty()) {
            const auto contract = Entry<NexusGetNativePatchContractFn>(moduleHandle_, NEXUS_NATIVE_PATCH_CONTRACT_ENTRY);
            if(!contract || !Entry<NexusRebindGameSystemsFn>(moduleHandle_, NEXUS_NATIVE_REBIND_ENTRY) || contract(1, &patchContract_) != NEXUS_GAME_OK || !ValidPatchContract(patchContract_)) {
                Stop(); return std::unexpected(Failure(path_, "explicit Native patch contract / rebind export is missing or invalid."));
            }
            patchSources_ = PatchSources(settings.patchSourceDirectory);
            if(stopRequested_) return std::unexpected(Failure(path_, "start cancelled during patch contract callback."));
        }
        const auto created = module_.create(&context, &state_);
        if(stopRequested_) return std::unexpected(Failure(path_, "start cancelled during create callback."));
        if(created != NEXUS_GAME_OK || !state_) {
            Stop(); return std::unexpected(Failure(path_, "create failed (" + std::to_string(created) + ")."));
        }
        // System登録は既存V1 entryと別の任意export。ゲーム固有SystemをEngineへ固定しない。
        if(systemAddress || bindingAddress) {
            NexusRegisterGameSystemsFn registerSystems = nullptr;
            static_assert(sizeof(registerSystems) == sizeof(systemAddress)); std::memcpy(&registerSystems, &systemAddress, sizeof(registerSystems));
            systems_ = std::make_unique<NativeGameSystemScope>(*level, context.world, settings.moduleId);
            NexusGameStatus registered = NEXUS_GAME_FAILED;
            if(bindingAddress) {
                // V1のlayoutを変えず、対応DLLだけが別exportからBinding登録窓口を取得する。
                NexusRegisterGameSystemsV2Fn bindSystems = nullptr;
                static_assert(sizeof(bindSystems) == sizeof(bindingAddress)); std::memcpy(&bindSystems, &bindingAddress, sizeof(bindSystems));
                const auto registration = systems_->BindingContext(); registered = bindSystems(state_, &registration);
            } else {
                const auto registration = systems_->Context(); registered = registerSystems(state_, &registration);
            }
            if(stopRequested_) return std::unexpected(Failure(path_, "start cancelled during System registration."));
            if(registered != NEXUS_GAME_OK) { Stop(); return std::unexpected(Failure(path_, "Native System registration failed.")); }
            // 登録は初期化時に完了し、Update中の構成変更・不定な実行順を防ぐ。
            systems_->Seal();
        }
        // 既存byte-command公開は別責務。System登録や関数Metadataから自動的に呼出先を生成しない。
        const auto connectAddress = GetProcAddress(static_cast<HMODULE>(moduleHandle_), NEXUS_GAME_SERVICES_ENTRY);
        if(connectAddress) {
            NexusConnectGameServicesFn connect = nullptr;
            static_assert(sizeof(connect) == sizeof(connectAddress)); std::memcpy(&connect, &connectAddress, sizeof(connect));
            if(GameApiBridge::OpenNativeServices(context.world, &servicesClient_) != NEXUS_GAME_OK) { Stop(); return std::unexpected(Failure(path_, "cannot create a services client.")); }
            const NexusGameServicesContextV1 services{1, sizeof(NexusGameServicesContextV1), context.world, servicesClient_, GameApiBridge::Services()};
            if(connect(state_, &services) != NEXUS_GAME_OK) { Stop(); return std::unexpected(Failure(path_, "Public API registration failed.")); }
            if(stopRequested_) return std::unexpected(Failure(path_, "start cancelled during Public API registration."));
        }
        NEXUS_LOG_INFO("GameModule", "Loaded Native Game Module: " + path_);
        if(stopRequested_) return std::unexpected(Failure(path_, "start cancelled by a completion callback."));
        return {};
    } catch(...) {
        Stop();
        return std::unexpected(Failure(path_, "callback violated its no-exception contract or loading failed."));
    }
}

Result<void> GameModuleLoader::Update(float deltaTime) {
    if(thread_ != std::this_thread::get_id()) return std::unexpected(Failure(path_, "update requires the owning thread."));
    if(operationActive_ || stopping_ || patchPrepared_ || patchFailed_ || !moduleHandle_ || !state_ || !std::isfinite(deltaTime) || deltaTime < 0)
        return std::unexpected(Failure(path_, "invalid update state or deltaTime."));
    OperationGuard operation{*this};
    try {
        const auto status = module_.update(state_, deltaTime);
        if(stopRequested_) return std::unexpected(Failure(path_, "stop requested during update callback."));
        if(status == NEXUS_GAME_OK) return {};
        return std::unexpected(Failure(path_, "update failed (" + std::to_string(status) + ")."));
    } catch(...) { return std::unexpected(Failure(path_, "update violated its no-exception contract.")); }
}
Result<std::shared_ptr<void>> GameModuleLoader::RetainCode() {
    if(thread_!=std::this_thread::get_id() || operationActive_ || patchPrepared_ || patchFailed_ || !moduleHandle_)
        return std::unexpected(Failure(path_,"Code lease requires an idle running Module."));
    HMODULE retained=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(moduleHandle_),&retained))
        return std::unexpected(Failure(path_,"Cannot retain the Module image for Level transition."));
    return std::shared_ptr<void>(retained,[](void* image) { if(image) FreeLibrary(static_cast<HMODULE>(image)); });
}
Result<void> GameModuleLoader::PreparePatch() {
    if(thread_ != std::this_thread::get_id() || operationActive_ || stopping_ || patchPrepared_ || patchFailed_ || quarantined_ || !moduleHandle_ || !state_ || !systems_ || identity_.patchSourceDirectory.empty())
        return std::unexpected(Failure(path_, "Native patch preparation requires an explicit contract and idle owning Module."));
    OperationGuard operation{*this};
    try {
        const auto contract = Entry<NexusGetNativePatchContractFn>(moduleHandle_, NEXUS_NATIVE_PATCH_CONTRACT_ENTRY);
        NexusNativePatchContractV1 current{};
        if(PatchSources(identity_.patchSourceDirectory) != patchSources_ || !contract || contract(1, &current) != NEXUS_GAME_OK || !SamePatchContract(patchContract_, current) || !SameComponentMetadata(moduleHandle_, identity_))
            return std::unexpected(Failure(path_, "Native header / translation units / state contract changed; rebuild and restart."));
        if(stopRequested_) return std::unexpected(Failure(path_, "stop requested during patch preparation."));
        // 明示exportは対で要求する。固定rootの互換性検証を通した後に、値だけを採取する。
        nativeState_.clear(); nativeStateSchema_=0;
        const auto save=Entry<NexusSaveNativeGameStateFn>(moduleHandle_,NEXUS_NATIVE_SAVE_STATE_ENTRY);
        const auto restore=Entry<NexusRestoreNativeGameStateFn>(moduleHandle_,NEXUS_NATIVE_RESTORE_STATE_ENTRY);
        if(static_cast<bool>(save)!=static_cast<bool>(restore)) return std::unexpected(Failure(path_,"Native state transfer requires both save and restore exports."));
        if(save) {
            uint32_t length=0; uint64_t schema=0;
            const auto measured=save(state_,1,&schema,nullptr,0,&length);
            if((measured!=NEXUS_GAME_OK && measured!=NEXUS_GAME_BUFFER_TOO_SMALL) || !schema || length>NEXUS_NATIVE_STATE_MAX_BYTES)
                return std::unexpected(Failure(path_,"Native state snapshot measurement failed or exceeds 1 MiB."));
            std::vector<uint8_t> snapshot(length); uint32_t copied=0; uint64_t copiedSchema=0;
            if(save(state_,1,&copiedSchema,snapshot.data(),length,&copied)!=NEXUS_GAME_OK || copied!=length || copiedSchema!=schema || stopRequested_)
                return std::unexpected(Failure(path_,"Native state snapshot copy failed or changed its schema/length."));
            nativeState_=std::move(snapshot); nativeStateSchema_=schema;
        }
        if(!systems_->PreparePatch()) return std::unexpected(Failure(path_, "Native System callbacks are busy or not patchable."));
        patchPrepared_ = true; return {};
    } catch(const std::exception& exception) { return std::unexpected(Failure(path_, exception.what())); }
    catch(...) { return std::unexpected(Failure(path_, "Native patch preparation failed.")); }
}
Result<void> GameModuleLoader::FinishPatch(bool applied) {
    if(thread_ != std::this_thread::get_id() || operationActive_ || stopping_ || !patchPrepared_ || !systems_)
        return std::unexpected(Failure(path_, "Native patch completion requires its prepared owner thread."));
    // 適用済み通知の後に再登録が失敗しても、非適用へ戻したことにして旧表を再開してはならない。
    if(!applied && patchFailed_)
        return std::unexpected(Failure(path_, "An applied Native patch cannot be cancelled after rebind failure."));
    OperationGuard operation{*this};
    if(!applied) {
        if(!systems_->FinishPatch(false)) return std::unexpected(Failure(path_, "Cannot resume the unmodified Native Module."));
        nativeState_.clear(); nativeStateSchema_=0; // Compile失敗では元stateが有効。Restoreを呼んで副作用を追加しない。
        patchPrepared_ = false; return {};
    }
    patchFailed_ = true;
    bool stateCompatible = false;
    try {
        // まずstateを参照しないexportだけを呼ぶ。ABI違反時は新destroyを旧heapへ適用しない。
        const auto contract = Entry<NexusGetNativePatchContractFn>(moduleHandle_, NEXUS_NATIVE_PATCH_CONTRACT_ENTRY); NexusNativePatchContractV1 current{};
        if(PatchSources(identity_.patchSourceDirectory) != patchSources_ || !contract || contract(1, &current) != NEXUS_GAME_OK || !SamePatchContract(patchContract_, current) || !SameComponentMetadata(moduleHandle_, identity_)) {
            unsafePatch_ = true; quarantined_ = true; systems_->Abandon(); GameApiBridge::BlockLegacyPatches();
            return std::unexpected(Failure(path_, "Applied patch violated the fixed state contract. DLL / state pinned; restart the process."));
        }
        const auto getModule = Entry<NexusGetGameModuleFn>(moduleHandle_, NEXUS_GAME_MODULE_ENTRY); NexusGameModuleV1 refreshed{};
        if(!getModule || getModule(NEXUS_GAME_ABI_VERSION, &refreshed) != NEXUS_GAME_OK || refreshed.abiVersion != module_.abiVersion || refreshed.structSize != sizeof(refreshed) ||
            !refreshed.create || !refreshed.update || !refreshed.destroy || !Match(refreshed.engineId, identity_.engineId) || !Match(refreshed.engineVersion, identity_.engineVersion) ||
            !Match(refreshed.projectId, identity_.projectId) || !Match(refreshed.moduleId, identity_.moduleId)) throw std::runtime_error("Patched Module identity / ABI is incompatible.");
        stateCompatible = true;
        module_=refreshed;
        if(nativeStateSchema_) {
            // 適用後のexportを再解決する。古いゲーム関数pointerをsnapshotへ保存しない。
            const auto restore=Entry<NexusRestoreNativeGameStateFn>(moduleHandle_,NEXUS_NATIVE_RESTORE_STATE_ENTRY);
            if(!systems_->RestoreState(restore,state_,nativeStateSchema_,nativeState_))
                throw std::runtime_error("Native state restore failed; execution remains suspended. Restart or stop the game.");
        }
        const auto rebind = Entry<NexusRebindGameSystemsFn>(moduleHandle_, NEXUS_NATIVE_REBIND_ENTRY); const auto context = systems_->BindingContext();
        if(!rebind || rebind(state_, &context) != NEXUS_GAME_OK || !systems_->FinishPatch(true)) throw std::runtime_error("Patched Native API rebind is incomplete; World remains suspended.");
        module_ = refreshed; patchPrepared_ = false; patchFailed_ = false;
        nativeState_.clear(); nativeStateSchema_=0;
        if(stopRequested_) return std::unexpected(Failure(path_, "stop requested during patch completion."));
        return {};
    } catch(const std::exception& exception) { if(!stateCompatible) AbandonPatch(); return std::unexpected(Failure(path_, exception.what())); }
    catch(...) { if(!stateCompatible) AbandonPatch(); return std::unexpected(Failure(path_, "Native patch completion failed; World remains suspended.")); }
}
bool GameModuleLoader::ValidatePreparedPatch() noexcept {
    if(thread_ != std::this_thread::get_id() || operationActive_ || stopping_ || !patchPrepared_ || patchFailed_ || unsafePatch_ || !moduleHandle_) return false;
    OperationGuard operation{*this};
    try {
        const auto contract = Entry<NexusGetNativePatchContractFn>(moduleHandle_, NEXUS_NATIVE_PATCH_CONTRACT_ENTRY);
        NexusNativePatchContractV1 current{};
        return PatchSources(identity_.patchSourceDirectory) == patchSources_ && contract &&
            contract(1, &current) == NEXUS_GAME_OK && SamePatchContract(patchContract_, current) && SameComponentMetadata(moduleHandle_, identity_) && !stopRequested_;
    } catch(...) { return false; }
}
void GameModuleLoader::AbandonPatch() noexcept {
    if(thread_ != std::this_thread::get_id()) return;
    unsafePatch_ = true; patchFailed_ = true; quarantined_ = true;
    if(systems_) systems_->Abandon();
    GameApiBridge::BlockLegacyPatches();
}

void GameModuleLoader::Stop() noexcept {
    if(thread_ != std::this_thread::get_id()) {
        // 誤Threadから実行中のstateやguardを書き換えない。所有Threadによる停止の再試行が必要。
        if(thread_ != std::thread::id{}) NEXUS_LOG_ERROR("GameModule", "Native Module Stop requires its owning thread; retry shutdown on that thread.");
        return;
    }
    if(stopping_) return; // destroy / System終了callbackからの二重破棄を拒否する。
    if(operationActive_) { stopRequested_ = true; return; }
    if(!moduleHandle_) return;
    stopping_ = true;
    struct Reset { bool& flag; ~Reset() { flag = false; } } reset{stopping_};
    // 完了通知前は適用の有無を判断できない。新旧どちらのdestroyも呼ばずコードとstateを保持する。
    if(patchPrepared_ && !unsafePatch_) AbandonPatch();
    bool canUnload = thread_ == std::this_thread::get_id() && !unsafePatch_;
    if(servicesClient_) {
        if(canUnload) {
            const auto closed = GameApiBridge::Services().close(servicesClient_);
            canUnload = closed == NEXUS_GAME_OK || closed == NEXUS_GAME_INVALID_HANDLE;
        }
        servicesClient_ = 0;
    }
    if(systems_) {
        // 公開APIを先に無効化し、System state / adapterを逆登録順で破棄してからModule stateへ進む。
        const bool stopped = systems_->Close(); canUnload = canUnload && stopped;
        systems_.reset();
    }
    if(state_ && canUnload) {
        try { canUnload = module_.destroy && module_.destroy(state_) == NEXUS_GAME_OK; }
        catch(...) { canUnload = false; }
    }
    // 関数ポインタやmetadataをUnload後に参照させない。stateの解放はDLLだけが行う。
    state_ = nullptr;
    module_ = {};
    patchPrepared_ = false; patchSources_.clear(); nativeState_.clear(); nativeStateSchema_=0;
    const auto handle = static_cast<HMODULE>(moduleHandle_);
    moduleHandle_ = nullptr;
    if(!canUnload) {
        quarantined_ = true;
        GameApiBridge::BlockLegacyPatches();
        // 停止できなかったcallbackのコードを解放するより、Process終了までDLLを保持する。
        // World tokenは直後にRuntimeが失効させるため、残留callbackもLevelへ到達できない。
        NEXUS_LOG_ERROR("GameModule", "Native Module destroy failed; DLL is pinned until process exit.");
        return;
    }
    if(!FreeLibrary(handle)) {
        quarantined_ = true;
        GameApiBridge::BlockLegacyPatches();
        NEXUS_LOG_ERROR("GameModule", "FreeLibrary failed; Native Module reference remains loaded.");
    }
}
}
