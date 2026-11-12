#include "MonoHost.h"
#include "Foundation/Logging/Logger.h"
#include "Foundation/Utility/String/ConvertString.h"
#include "Platform/FileSystem/AtomicFile.h"
#include "Runtime/Project/ProjectJson.h"
#include <Windows.h>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
#include <stdexcept>

namespace NexusEngine {
namespace {
// Mono公式headerのopaque型とCdecl署名だけを宣言する。SDK内部構造へはアクセスしない。
struct MonoDomain;
struct MonoAssembly;
struct MonoImage;
struct MonoClass;
struct MonoMethod;
struct MonoObject;
struct MonoString;
struct MonoThread;

Error Failure(std::string message) { return Error(ErrorCategory::Framework, 239, "Mono: " + std::move(message)); }
/*-----------------------------------------------------------------------------------------
 * MonoProcess
 * - Mono rootと公式exportをProcess寿命で保持する。子Domainは各MonoHostが所有する
 * - mono_jit_cleanup後の再初期化は公式に非対応のため、Play停止ではrootを終了しない
 *---------------------------------------------------------------------------------------*/
struct MonoProcess {
    std::mutex mutex;
    HMODULE library = nullptr;
    MonoDomain* root = nullptr;
    std::filesystem::path directory;
    bool failed = false;
    void (__cdecl* setDirs)(const char*, const char*) = nullptr;
    void (__cdecl* configParse)(const char*) = nullptr;
    MonoDomain* (__cdecl* initialize)(const char*, const char*) = nullptr;
    MonoDomain* (__cdecl* createDomain)(char*, char*) = nullptr;
    void (__cdecl* configureDomain)(MonoDomain*, const char*, const char*) = nullptr;
    int (__cdecl* setDomain)(MonoDomain*, int) = nullptr;
    MonoThread* (__cdecl* attachThread)(MonoDomain*) = nullptr;
    void (__cdecl* unloadDomain)(MonoDomain*, MonoObject**) = nullptr;
    MonoAssembly* (__cdecl* openAssembly)(MonoDomain*, const char*) = nullptr;
    MonoImage* (__cdecl* assemblyImage)(MonoAssembly*) = nullptr;
    MonoClass* (__cdecl* findClass)(MonoImage*, const char*, const char*) = nullptr;
    MonoMethod* (__cdecl* findMethod)(MonoClass*, const char*, int) = nullptr;
    MonoObject* (__cdecl* invoke)(MonoMethod*, void*, void**, MonoObject**) = nullptr;
    void* (__cdecl* unbox)(MonoObject*) = nullptr;
    MonoString* (__cdecl* newString)(MonoDomain*, const char*) = nullptr;
    char* (__cdecl* stringUtf8)(MonoString*) = nullptr;
    MonoString* (__cdecl* objectString)(MonoObject*, MonoObject**) = nullptr;
    void (__cdecl* freeMemory)(void*) = nullptr;
    uint32_t (__cdecl* rootObject)(MonoObject*, int) = nullptr;
    MonoObject* (__cdecl* rootedObject)(uint32_t) = nullptr;
    void (__cdecl* releaseObject)(uint32_t) = nullptr;
};
MonoProcess& ProcessRuntime() { static MonoProcess process; return process; }
template<class Function> void Bind(HMODULE library, const char* name, Function& target) {
    const auto address = GetProcAddress(library, name);
    if(!address) throw std::runtime_error(std::string("Missing embedding export: ") + name);
    static_assert(sizeof(target) == sizeof(address)); std::memcpy(&target, &address, sizeof(target));
}
Result<void> InitializeProcess(const std::filesystem::path& directory) {
    auto& api = ProcessRuntime();
    std::lock_guard lock(api.mutex);
    if(api.failed) return std::unexpected(Failure("Runtime initialization previously failed; restart the host."));
    if(api.root) {
        std::error_code error;
        if(!std::filesystem::equivalent(api.directory, directory, error) || error)
            return std::unexpected(Failure("A process cannot mix different Mono SDK roots."));
        return {};
    }
    try {
        const auto runtime = directory / "bin/mono-2.0-sgen.dll";
        const auto core = directory / "lib/mono/4.5/mscorlib.dll";
        if(!directory.is_absolute() || !std::filesystem::is_regular_file(runtime) || !std::filesystem::is_regular_file(core))
            return std::unexpected(Failure("Select an absolute Mono root containing bin/mono-2.0-sgen.dll and lib/mono/4.5/mscorlib.dll."));
        api.library = LoadLibraryExW(runtime.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if(!api.library) return std::unexpected(Failure("Cannot load Mono runtime: " + std::to_string(GetLastError())));
        api.failed = true;
        // MonoのGC / workerはsessionより長く動く。DLLをProcessへpinし、Domain停止でコードを解放しない。
        HMODULE pinned = nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, runtime.c_str(), &pinned)) throw std::runtime_error("Cannot pin Mono runtime.");
        Bind(api.library, "mono_set_dirs", api.setDirs); Bind(api.library, "mono_config_parse", api.configParse);
        Bind(api.library, "mono_jit_init_version", api.initialize); Bind(api.library, "mono_domain_create_appdomain", api.createDomain);
        Bind(api.library, "mono_domain_set_config", api.configureDomain); Bind(api.library, "mono_domain_set", api.setDomain);
        Bind(api.library, "mono_thread_attach", api.attachThread); Bind(api.library, "mono_domain_try_unload", api.unloadDomain);
        Bind(api.library, "mono_domain_assembly_open", api.openAssembly); Bind(api.library, "mono_assembly_get_image", api.assemblyImage);
        Bind(api.library, "mono_class_from_name", api.findClass); Bind(api.library, "mono_class_get_method_from_name", api.findMethod);
        Bind(api.library, "mono_runtime_invoke", api.invoke); Bind(api.library, "mono_object_unbox", api.unbox);
        Bind(api.library, "mono_string_new", api.newString); Bind(api.library, "mono_string_to_utf8", api.stringUtf8);
        Bind(api.library, "mono_object_to_string", api.objectString); Bind(api.library, "mono_free", api.freeMemory);
        Bind(api.library, "mono_gchandle_new", api.rootObject); Bind(api.library, "mono_gchandle_get_target", api.rootedObject);
        Bind(api.library, "mono_gchandle_free", api.releaseObject);
        const auto libraries = ProjectJson::Utf8(directory / "lib");
        const auto configuration = ProjectJson::Utf8(directory / "etc");
        const auto configFile = ProjectJson::Utf8(directory / "etc/mono/config");
        api.setDirs(libraries.c_str(), configuration.c_str()); api.configParse(configFile.c_str());
        api.root = api.initialize("NexusEngine", "v4.0.30319");
        if(!api.root) throw std::runtime_error("mono_jit_init_version failed.");
        api.directory = directory; api.failed = false;
        NEXUS_LOG_SUCCESS("MonoHost", "Mono SGen runtime initialized.");
        return {};
    } catch(const std::exception& exception) { return std::unexpected(Failure(exception.what())); }
}
std::string Text(MonoString* value) {
    if(!value) return {};
    auto& api = ProcessRuntime();
    char* bytes = api.stringUtf8(value);
    if(!bytes) return {};
    std::string result(bytes); api.freeMemory(bytes); return result;
}
std::string ExceptionText(MonoObject* exception) {
    auto& api = ProcessRuntime(); MonoObject* nested = nullptr;
    const auto text = api.objectString(exception, &nested);
    return nested ? "Managed exception formatting failed." : Text(text);
}
/*-----------------------------------------------------------------------------------------
 * MonoDomainState
 * - 子Domainが存続する間だけMonoのmethod metadataを借用する
 * - Native側はManaged objectを保存しない。shadowはDomain解放成功後にだけ削除する
 *---------------------------------------------------------------------------------------*/
struct MonoDomainState {
    MonoDomain* domain = nullptr;
    MonoClass* entries = nullptr;
    std::filesystem::path shadow;
    bool unloadFailed = false; //< 解放未確認のshadowは、Host破棄時にも削除しない
};
/*-----------------------------------------------------------------------------------------
 * MonoStringArgument
 * - 次のManaged割当で文字列が移動してもNative argvを無効にしない短命のGC root
 * - 呼出し完了時に解放し、Domain unloadを跨いでGCHandleを保存しない
 *---------------------------------------------------------------------------------------*/
class MonoStringArgument final {
public:
    MonoStringArgument(MonoDomain* domain, const std::string& value) {
        auto& api = ProcessRuntime();
        handle_ = api.rootObject(reinterpret_cast<MonoObject*>(api.newString(domain, value.c_str())), 1);
    }
    ~MonoStringArgument() { ProcessRuntime().releaseObject(handle_); }
    MonoStringArgument(const MonoStringArgument&) = delete;
    MonoStringArgument& operator=(const MonoStringArgument&) = delete;
    void* Get() const { return ProcessRuntime().rootedObject(handle_); }
private:
    uint32_t handle_ = 0; //< 同期invoke中だけ所有するMono GCHandle
};
Result<MonoObject*> Invoke(MonoDomainState& domain, const char* name, std::vector<void*> arguments) {
    auto& api = ProcessRuntime();
    if(!domain.domain || !api.setDomain(domain.domain, 1)) return std::unexpected(Failure("Domain is unavailable."));
    const auto method = api.findMethod(domain.entries, name, static_cast<int>(arguments.size()));
    if(!method) return std::unexpected(Failure(std::string("Bootstrap method is missing: ") + name));
    MonoObject* exception = nullptr;
    auto* result = api.invoke(method, nullptr, arguments.empty() ? nullptr : arguments.data(), &exception);
    if(exception) return std::unexpected(Failure(std::string(name) + ": " + ExceptionText(exception)));
    return result;
}
void RemoveShadow(std::filesystem::path& shadow) noexcept {
    if(shadow.empty()) return;
    try {
        // 作成済みGUID直下だけが所有範囲。SDKやProjectのSourceへ削除先を広げない。
        const auto root = std::filesystem::temp_directory_path() / "NexusEngine.Mono";
        if(shadow.parent_path() != root || shadow.filename().empty()) {
            NEXUS_LOG_ERROR("MonoHost", "Rejected shadow cleanup outside the owned Mono directory."); return;
        }
        std::error_code error; std::filesystem::remove_all(shadow, error);
        if(error) NEXUS_LOG_WARNING("MonoHost", "Cannot remove retired Mono shadow: " + error.message());
        shadow.clear();
    } catch(...) { NEXUS_LOG_WARNING("MonoHost", "Mono shadow cleanup failed."); }
}
bool CloseDomain(MonoDomainState& domain, bool removeShadow = true) noexcept {
    if(domain.unloadFailed) return false;
    if(!domain.domain) { if(removeShadow) RemoveShadow(domain.shadow); return true; }
    auto& api = ProcessRuntime();
    api.setDomain(api.root, 1);
    MonoObject* exception = nullptr;
    api.unloadDomain(domain.domain, &exception);
    // 例外があればDomainの解放成功を主張しない。DLLはpinされ、呼出側をquarantineする。
    if(exception) {
        // 失敗したDomainの有効性は不明なため、同じpointerへ二度目のunloadを試みない。
        domain.domain = nullptr; domain.entries = nullptr;
        domain.unloadFailed = true;
        NEXUS_LOG_ERROR("MonoHost", "Mono Domain unload failed; restart the host process."); return false;
    }
    domain.domain = nullptr; domain.entries = nullptr;
    if(removeShadow) RemoveShadow(domain.shadow);
    return true;
}
/*-----------------------------------------------------------------------------------------
 * MonoDomainCleanup
 * - 部分初期化・C++例外時にも子Domainを終了する短命の所有guard
 * - unload失敗時は再利用を禁止し、解放未確認のshadowを残す
 *---------------------------------------------------------------------------------------*/
struct MonoDomainCleanup {
    MonoDomainState& value;
    bool* quarantined = nullptr;
    bool owned = true;
    ~MonoDomainCleanup() {
        if(owned && !CloseDomain(value)) {
            if(quarantined) *quarantined = true;
            else { auto& api = ProcessRuntime(); std::lock_guard lock(api.mutex); api.failed = true; }
        }
    }
};
Result<MonoDomainState> CreateDomain(const std::filesystem::path& bootstrap) {
    auto& api = ProcessRuntime();
    if(!api.attachThread(api.root)) return std::unexpected(Failure("Cannot attach the owner thread."));
    auto uuid = CreateUuid(); if(!uuid) return std::unexpected(uuid.error());
    MonoDomainState domain;
    MonoDomainCleanup cleanup{domain};
    domain.shadow = std::filesystem::temp_directory_path() / "NexusEngine.Mono" / *uuid;
    std::filesystem::create_directories(domain.shadow);
    std::string name = "Nexus.Game:" + *uuid;
    domain.domain = api.createDomain(name.data(), nullptr);
    if(!domain.domain) return std::unexpected(Failure("Cannot create child Domain."));
    const auto base = ProjectJson::Utf8(bootstrap.parent_path());
    api.configureDomain(domain.domain, base.c_str(), "");
    api.setDomain(domain.domain, 1);
    const auto file = ProjectJson::Utf8(bootstrap);
    auto* assembly = api.openAssembly(domain.domain, file.c_str());
    if(assembly) domain.entries = api.findClass(api.assemblyImage(assembly), "Nexus.Managed.Bootstrap", "EntryPoints");
    if(!domain.entries) return std::unexpected(Failure("Cannot load Mono Bootstrap EntryPoints."));
    const auto shadow = ProjectJson::Utf8(domain.shadow);
    const auto configured = [&] {
        const MonoStringArgument argument(domain.domain, shadow);
        return Invoke(domain, "ConfigureDomain", {argument.Get()});
    }();
    if(!configured) return std::unexpected(configured.error());
    cleanup.owned = false;
    return domain;
}
}
struct MonoHost::State {
    MonoDomainState current;
    std::filesystem::path bootstrap, preparedAssembly, preparedShadow;
    std::thread::id thread;
    uint64_t nextInstance = 1;
    std::string snapshot; //< 再試行用の公開状態。旧Assembly / Game objectへの参照を含まない
    bool quarantined = false;
};
MonoHost::MonoHost() = default;
MonoHost::~MonoHost() { static_cast<void>(Shutdown()); }
Result<void> MonoHost::Initialize(const std::filesystem::path& root, const std::filesystem::path& bootstrap) {
    if(state_) return std::unexpected(Failure("Host is already initialized."));
    if(!bootstrap.is_absolute() || !std::filesystem::is_regular_file(bootstrap)) return std::unexpected(Failure("Bootstrap must be an existing absolute file."));
    auto process = InitializeProcess(root); if(!process) return process;
    auto state = std::make_unique<State>();
    state->bootstrap = bootstrap; state->thread = std::this_thread::get_id();
    auto domain = CreateDomain(bootstrap); if(!domain) return std::unexpected(domain.error());
    state->current = std::move(*domain); state_ = std::move(state);
    return {};
}
Result<void> MonoHost::Enter() {
    if(!state_ || state_->quarantined || state_->thread != std::this_thread::get_id() || !state_->current.domain)
        return std::unexpected(Failure("Callback requires a live Domain on its owner thread."));
    if(!ProcessRuntime().setDomain(state_->current.domain, 1)) return std::unexpected(Failure("Cannot select the callback Domain."));
    return {};
}
Result<void*> MonoHost::Resolve(const std::filesystem::path& assembly, const wchar_t* method) {
    auto entered = Enter(); if(!entered) return std::unexpected(entered.error());
    // 安定Bootstrap以外のAssemblyから関数を取得させず、Domain寿命とdelegateの所有者を一致させる。
    std::error_code error;
    if(!assembly.is_absolute() || !std::filesystem::equivalent(assembly, state_->bootstrap, error) || error)
        return std::unexpected(Failure("Entry lookup requires the configured Bootstrap assembly."));
    if(!method || !*method) return std::unexpected(Failure("Missing entry name."));
    const auto name = ConvertString(std::wstring(method));
    const MonoStringArgument argument(state_->current.domain, name);
    auto result = Invoke(state_->current, "ResolveNativeEntry", {argument.Get()});
    if(!result || !*result) return result ? std::unexpected(Failure("Null Native delegate.")) : std::unexpected(result.error());
    void* address = nullptr; std::memcpy(&address, ProcessRuntime().unbox(*result), sizeof(address));
    if(!address) return std::unexpected(Failure("Null Native delegate pointer."));
    return address;
}
Result<void> MonoHost::PrepareReload(uint64_t session, const std::filesystem::path& assembly) {
    auto entered = Enter(); if(!entered) return entered;
    if(!state_->preparedAssembly.empty()) return std::unexpected(Failure("Reload is already prepared."));
    if(session) {
        auto captured = Invoke(state_->current, "CaptureForDomainReload", {&session});
        if(!captured || !*captured) return captured ? std::unexpected(Failure("Missing reload snapshot.")) : std::unexpected(captured.error());
        state_->snapshot = Text(reinterpret_cast<MonoString*>(*captured));
        auto counter = Invoke(state_->current, "DomainNextInstance", {&session});
        if(!counter || !*counter) return counter ? std::unexpected(Failure("Missing instance counter.")) : std::unexpected(counter.error());
        std::memcpy(&state_->nextInstance, ProcessRuntime().unbox(*counter), sizeof(state_->nextInstance));
    } else if(state_->snapshot.empty()) return std::unexpected(Failure("No recoverable reload snapshot."));
    auto candidate = CreateDomain(state_->bootstrap); if(!candidate) return std::unexpected(candidate.error());
    MonoDomainCleanup cleanup{*candidate, &state_->quarantined};
    const auto file = ProjectJson::Utf8(assembly);
    // GC rootはDomain unloadより先に解除する。scopeの順序をNativeの値コピーと分ける。
    auto checked = [&] {
        const MonoStringArgument fileValue(candidate->domain, file);
        const MonoStringArgument stateValue(candidate->domain, state_->snapshot);
        return Invoke(*candidate, "PreflightDomainReload", {fileValue.Get(), stateValue.Get()});
    }();
    std::filesystem::path staged;
    if(checked && *checked) staged = std::filesystem::path(ConvertString(Text(reinterpret_cast<MonoString*>(*checked))));
    cleanup.owned = false;
    if(!CloseDomain(*candidate, staged.empty())) {
        state_->quarantined = true; return std::unexpected(Failure("Candidate Domain did not unload."));
    }
    if(!checked) return std::unexpected(checked.error());
    if(staged.empty()) return std::unexpected(Failure("Preflight produced no Assembly."));
    state_->preparedAssembly = std::move(staged); state_->preparedShadow = std::move(candidate->shadow);
    return Enter();
}
Result<void> MonoHost::BeginRetirement(uint64_t session) {
    auto entered = Enter(); if(!entered) return entered;
    if(!session) return {};
    auto counter = Invoke(state_->current, "DomainNextInstance", {&session});
    if(!counter || !*counter) return counter ? std::unexpected(Failure("Missing instance counter.")) : std::unexpected(counter.error());
    uint64_t next = 0; std::memcpy(&next, ProcessRuntime().unbox(*counter), sizeof(next));
    if(next > state_->nextInstance) state_->nextInstance = next;
    auto result = Invoke(state_->current, "BeginDomainRetirement", {&session});
    if(!result) return std::unexpected(result.error());
    return {};
}
Result<void> MonoHost::ActivatePrepared() {
    auto entered = Enter(); if(!entered) return entered;
    if(state_->preparedAssembly.empty()) return std::unexpected(Failure("No prepared Assembly."));
    if(!CloseDomain(state_->current)) { state_->quarantined = true; return std::unexpected(Failure("Old Domain did not unload.")); }
    auto next = CreateDomain(state_->bootstrap);
    if(!next) { state_->quarantined = true; return std::unexpected(next.error()); }
    state_->current = std::move(*next);
    auto seeded = Invoke(state_->current, "SetDomainInstanceSeed", {&state_->nextInstance});
    if(!seeded) { state_->quarantined = true; return std::unexpected(seeded.error()); }
    return {};
}
Result<void> MonoHost::MarkActivation(uint64_t session, bool pending) {
    auto entered = Enter(); if(!entered) return entered;
    uint8_t value = pending ? 1 : 0;
    auto result = Invoke(state_->current, "SetDomainActivation", {&session, &value});
    if(!result) return std::unexpected(result.error());
    return {};
}
void MonoHost::CancelPrepared() noexcept {
    if(!state_) return;
    RemoveShadow(state_->preparedShadow); state_->preparedAssembly.clear();
}
const std::filesystem::path& MonoHost::PreparedAssembly() const noexcept {
    static const std::filesystem::path empty;
    return state_ ? state_->preparedAssembly : empty;
}
bool MonoHost::Shutdown() noexcept {
    if(!state_) return true;
    if(state_->thread != std::this_thread::get_id()) { state_->quarantined = true; return false; }
    CancelPrepared();
    if(!CloseDomain(state_->current)) { state_->quarantined = true; return false; }
    const bool clean = !state_->quarantined;
    state_.reset(); return clean;
}
bool MonoHost::IsQuarantined() const noexcept { return state_ && state_->quarantined; }
}
