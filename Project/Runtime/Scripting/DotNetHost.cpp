#include "DotNetHost.h"
#include "MonoHost.h"
#include "CSharpRuntimeSettings.h"
#include "Foundation/Logging/Logger.h"
#include "Foundation/Utility/String/ConvertString.h"
#include "Runtime/Project/ProjectJson.h"
#include <Windows.h>
#include "ThirdParty/DotNetHosting/nethost.h"
#include "ThirdParty/DotNetHosting/hostfxr.h"
#include "ThirdParty/DotNetHosting/coreclr_delegates.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <thread>
#include <vector>

namespace NexusEngine {
namespace {
// hostfxrのerror writerはthread-local。callback中だけ借用する出力先を同じthreadに置く。
thread_local std::string* hostErrors = nullptr;

/** \brief 外部Hostingのエラーを既存Loggerへ転送し、callback外へC++例外を出さない */
void HOSTFXR_CALLTYPE HostError(const char_t* message) noexcept {
    try {
        const auto text = message ? ConvertString(std::wstring(message)) : std::string("Unknown hostfxr error.");
        if(hostErrors && hostErrors->size() < 16384) { hostErrors->append(text.substr(0, 16384 - hostErrors->size())); hostErrors->append("\n"); }
        NEXUS_LOG_ERROR("DotNetHost", text);
    } catch(...) { NEXUS_LOG_ERROR("DotNetHost", "Cannot convert hostfxr diagnostic to UTF-8."); }
}

/*-----------------------------------------------------------------------------------------
 * ErrorWriterScope
 * - Hosting呼出し中だけerror writerを置換し、先行するホストのcallbackを必ず復元する
 *---------------------------------------------------------------------------------------*/
class ErrorWriterScope final {
public:
    ErrorWriterScope(hostfxr_set_error_writer_fn setter, std::string& errors) noexcept
        : setter_(setter), previous_(setter(HostError)), previousErrors_(hostErrors) { hostErrors = &errors; }
    ~ErrorWriterScope() { setter_(previous_); hostErrors = previousErrors_; }
    ErrorWriterScope(const ErrorWriterScope&) = delete;
    ErrorWriterScope& operator=(const ErrorWriterScope&) = delete;
private:
    hostfxr_set_error_writer_fn setter_; //< 呼出し中はhostfxr DLL参照が生存する
    hostfxr_error_writer_fn previous_; //< 他のホストが設定していたthread-local callback
    std::string* previousErrors_; //< 再入時に復元する借用先
};

/** \brief Windows関数ポインタの表現だけをコピーし、独自のHosting ABI定義を作らない */
template<class Function>
Function Export(HMODULE library, const char* name) noexcept {
    const auto address = GetProcAddress(library, name);
    Function function = nullptr;
    static_assert(sizeof(function) == sizeof(address));
    std::memcpy(&function, &address, sizeof(function));
    return function;
}
Error Failed(const std::string& operation, int32_t status, const std::string& details = {}) {
    char code[16]{};
    std::snprintf(code, sizeof(code), "0x%08X", static_cast<unsigned int>(status));
    return Error(ErrorCategory::Framework, 230, "DotNetHost " + operation + " failed (" + code + "). " + details);
}
}

/*-----------------------------------------------------------------------------------------
 * DotNetHost::State
 * - Hosting DLLの参照とProcess寿命のloader delegateを管理する
 * - CoreCLRの所有権・Unload機能は持たず、host contextも初期化処理内で閉じる
 *---------------------------------------------------------------------------------------*/
struct DotNetHost::State {
    HMODULE nethost = nullptr; //< このHostが取得した公式nethost.dll参照
    HMODULE hostfxr = nullptr; //< セッションの参照。Hosting状態自体はProcess終了までpinして維持する
    hostfxr_set_error_writer_fn writer = nullptr; //< 公式error callback登録関数
    load_assembly_and_get_function_pointer_fn load = nullptr; //< 公式Managed component loader
    std::thread::id thread; //< 初期化とResolveを直列実行する所有thread
    ~State() {
        load = nullptr; writer = nullptr;
        if(hostfxr && !FreeLibrary(hostfxr)) NEXUS_LOG_ERROR("DotNetHost", "Cannot release hostfxr library reference.");
        if(nethost && !FreeLibrary(nethost)) NEXUS_LOG_ERROR("DotNetHost", "Cannot release nethost library reference.");
    }
};

DotNetHost::DotNetHost() = default;
DotNetHost::~DotNetHost() { Shutdown(); }

Result<void> DotNetHost::Initialize(const CSharpRuntimeSettings& settings) {
    if(state_ || mono_ || quarantined_) return std::unexpected(Failed("initialize", -1, "Host is already initialized or quarantined."));
    if(!settings.monoRoot.empty()) {
        auto mono = std::make_unique<MonoHost>();
        auto result = mono->Initialize(settings.monoRoot, settings.bootstrapAssembly);
        if(!result) return result;
        mono_ = std::move(mono); return {};
    }
    try {
        for(const auto* file : {&settings.nethostFile, &settings.runtimeConfig, &settings.bootstrapAssembly}) {
            std::error_code error;
            if(!file->is_absolute() || !std::filesystem::is_regular_file(*file, error) || error)
                return std::unexpected(Failed("validate file", -1, ProjectJson::Utf8(*file)));
        }
        if(!settings.dotnetRoot.empty() && (!settings.dotnetRoot.is_absolute() || !std::filesystem::is_directory(settings.dotnetRoot)))
            return std::unexpected(Failed("validate dotnetRoot", -1, ProjectJson::Utf8(settings.dotnetRoot)));
        auto state = std::make_unique<State>();
        state->thread = std::this_thread::get_id();
        state->nethost = LoadLibraryExW(settings.nethostFile.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if(!state->nethost) return std::unexpected(Failed("load nethost", static_cast<int32_t>(GetLastError()), ProjectJson::Utf8(settings.nethostFile)));
        const auto getPath = Export<decltype(&get_hostfxr_path)>(state->nethost, "get_hostfxr_path");
        if(!getPath) return std::unexpected(Failed("resolve get_hostfxr_path", static_cast<int32_t>(GetLastError())));
        const get_hostfxr_parameters find{sizeof(get_hostfxr_parameters), settings.bootstrapAssembly.c_str(),
            settings.dotnetRoot.empty() ? nullptr : settings.dotnetRoot.c_str()};
        size_t length = 0;
        auto status = getPath(nullptr, &length, &find);
        // 公式の二段階buffer取得。サイズ不足だけを期待する失敗として扱う。
        if((status != 0 && static_cast<uint32_t>(status) != 0x80008098u) || length == 0 || length > 32768)
            return std::unexpected(Failed("discover hostfxr", status));
        std::vector<char_t> path(length);
        status = getPath(path.data(), &length, &find);
        if(status != 0 || std::find(path.begin(), path.end(), L'\0') == path.end())
            return std::unexpected(Failed("discover hostfxr", status));
        const std::filesystem::path fxrPath(path.data());
        if(!fxrPath.is_absolute()) return std::unexpected(Failed("discover hostfxr", -1, "nethost returned a relative path."));
        state->hostfxr = LoadLibraryExW(fxrPath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if(!state->hostfxr) return std::unexpected(Failed("load hostfxr", static_cast<int32_t>(GetLastError()), ProjectJson::Utf8(fxrPath)));
        const auto initialize = Export<hostfxr_initialize_for_runtime_config_fn>(state->hostfxr, "hostfxr_initialize_for_runtime_config");
        const auto getDelegate = Export<hostfxr_get_runtime_delegate_fn>(state->hostfxr, "hostfxr_get_runtime_delegate");
        const auto close = Export<hostfxr_close_fn>(state->hostfxr, "hostfxr_close");
        state->writer = Export<hostfxr_set_error_writer_fn>(state->hostfxr, "hostfxr_set_error_writer");
        if(!initialize || !getDelegate || !close || !state->writer)
            return std::unexpected(Failed("resolve hostfxr exports", -1));
        // CoreCLR / hostpolicyはProcess内に残る。hostfxrだけをUnloadすると初期化済みの情報が
        // 失われ、次のPlayでhostpolicyへ誤った初回要求を送る。公式DLLをProcess寿命へpinし、
        // このStateの参照解放とRuntime停止を混同しない。error writerは引き続きscopeで復元する。
        HMODULE processHost = nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, fxrPath.c_str(), &processHost))
            return std::unexpected(Failed("retain process hostfxr", static_cast<int32_t>(GetLastError())));
        std::string errors;
        ErrorWriterScope diagnostics(state->writer, errors);
        hostfxr_handle context = nullptr;
        // error writerを復元するより先にcontextを閉じ、失敗・例外でも初回Hostを残さない。
        struct ContextCleanup {
            hostfxr_handle& contextRef; hostfxr_close_fn closeFunction;
            ~ContextCleanup() { if(contextRef && closeFunction(contextRef) != 0) NEXUS_LOG_ERROR("DotNetHost", "Cannot close failed host context."); }
        } cleanup{context, close};
        const hostfxr_initialize_parameters parameters{sizeof(hostfxr_initialize_parameters), nullptr,
            settings.dotnetRoot.empty() ? nullptr : settings.dotnetRoot.c_str()};
        status = initialize(settings.runtimeConfig.c_str(), &parameters, &context);
        // 0=初回、1=互換な既存Runtime。2の設定差は未検証のため成功として黙認しない。
        if((status != 0 && status != 1) || !context)
            return std::unexpected(Failed("initialize runtimeconfig " + ProjectJson::Utf8(settings.runtimeConfig), status, errors));
        void* delegate = nullptr;
        status = getDelegate(context, hdt_load_assembly_and_get_function_pointer, &delegate);
        if(status != 0 || !delegate) return std::unexpected(Failed("get runtime delegate", status, errors));
        static_assert(sizeof(state->load) == sizeof(delegate));
        std::memcpy(&state->load, &delegate, sizeof(delegate));
        status = close(context); context = nullptr;
        if(status != 0) return std::unexpected(Failed("close host context", status, errors));
        // hostfxr_close後もloader delegateとCoreCLRはProcess内で有効である。
        state_ = std::move(state);
        return {};
    } catch(const std::exception& exception) { return std::unexpected(Failed("initialize", -1, exception.what())); }
    catch(...) { return std::unexpected(Failed("initialize", -1, "Unknown native exception.")); }
}

Result<void*> DotNetHost::Resolve(const std::filesystem::path& assembly, const wchar_t* method) {
    if(mono_) return mono_->Resolve(assembly, method);
    if(!state_ || state_->thread != std::this_thread::get_id() || !assembly.is_absolute() || !method || !*method)
        return std::unexpected(Failed("resolve entry", -1, "Requires initialized host, owner thread and absolute assembly path."));
    try {
        std::string errors;
        ErrorWriterScope diagnostics(state_->writer, errors);
        void* entry = nullptr;
        const auto status = state_->load(assembly.c_str(), L"Nexus.Managed.Bootstrap.EntryPoints, Nexus.Managed.Bootstrap",
            method, UNMANAGEDCALLERSONLY_METHOD, nullptr, &entry);
        if(status != 0 || !entry) return std::unexpected(Failed("load Managed entry " + ConvertString(std::wstring(method)), status,
            ProjectJson::Utf8(assembly) + " " + errors));
        return entry;
    } catch(const std::exception& exception) { return std::unexpected(Failed("resolve entry", -1, exception.what())); }
    catch(...) { return std::unexpected(Failed("resolve entry", -1, "Unknown native exception.")); }
}

void DotNetHost::Shutdown() noexcept {
    // Managed sessionの停止は上位CSharpRuntimeが先に行う。CoreCLRのUnloadとは扱わない。
    if(mono_) { quarantined_ |= !mono_->Shutdown(); mono_.reset(); }
    state_.reset();
}
}
