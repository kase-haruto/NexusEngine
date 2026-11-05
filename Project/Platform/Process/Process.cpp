#include "Process.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <utility>

namespace NexusEngine {
Process::~Process() { if(job_) CloseHandle(job_); if(handle_) CloseHandle(handle_); }
Process::Process(Process&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)), id_(std::exchange(other.id_, 0)), job_(std::exchange(other.job_, nullptr)) {}
Process& Process::operator=(Process&& other) noexcept {
    if(this != &other) { if(job_) CloseHandle(job_); if(handle_) CloseHandle(handle_); handle_ = std::exchange(other.handle_, nullptr); id_ = std::exchange(other.id_, 0); job_ = std::exchange(other.job_, nullptr); }
    return *this;
}

////////////////////////////
// 引用符直前と末尾のbackslashをWindows規則で二重化する
////////////////////////////
std::wstring Process::QuoteArgument(const std::wstring& argument) {
    std::wstring quoted = L"\"";
    size_t slashes = 0;
    for(wchar_t ch : argument) {
        if(ch == L'\\') { ++slashes; continue; }
        quoted.append(ch == L'\"' ? slashes * 2 + 1 : slashes, L'\\');
        quoted += ch; slashes = 0;
    }
    quoted.append(slashes * 2, L'\\'); quoted += L'\"';
    return quoted;
}

////////////////////////////
// 実行ファイルとargvを分離し、cmd.exeの解釈を通さない
////////////////////////////
Result<Process> Process::Start(const std::filesystem::path& executable, const std::vector<std::wstring>& arguments,
                               const std::filesystem::path& directory, bool hidden, const std::filesystem::path& log, bool ownTree) {
    std::error_code error;
    if(!executable.is_absolute() || !std::filesystem::is_regular_file(executable, error) || error || !directory.is_absolute())
        return std::unexpected(Error(ErrorCategory::FileSystem, 180, "Process requires an existing absolute executable and absolute working directory."));
    std::wstring command = QuoteArgument(executable.wstring());
    for(const auto& argument : arguments) {
        if(argument.find(L'\0') != argument.npos) return std::unexpected(Error(ErrorCategory::General, 180, "Process argument contains NUL."));
        command += L' '; command += QuoteArgument(argument);
    }
    if(command.size() >= 32767) return std::unexpected(Error(ErrorCategory::General, 180, "Process command line is too long."));
    struct Handle { HANDLE value = INVALID_HANDLE_VALUE; ~Handle() { if(value && value != INVALID_HANDLE_VALUE) CloseHandle(value); } } output, input;
    STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(STARTUPINFOW);
    startup.StartupInfo.dwFlags = STARTF_USESHOWWINDOW; startup.StartupInfo.wShowWindow = hidden ? SW_HIDE : SW_SHOWNORMAL;
    std::vector<unsigned char> attributes;
    HANDLE inherited[2]{};
    struct Attributes { LPPROC_THREAD_ATTRIBUTE_LIST list = nullptr; ~Attributes() { if(list) DeleteProcThreadAttributeList(list); } } attributeList;
    DWORD flags = (hidden ? CREATE_NO_WINDOW : 0) | (ownTree ? CREATE_SUSPENDED : 0);
    if(!log.empty()) {
        std::filesystem::create_directories(log.parent_path(), error);
        if(error) return std::unexpected(Error(ErrorCategory::FileSystem, 180, "Cannot create process log directory."));
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        output.value = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        input.value = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr);
        if(output.value == INVALID_HANDLE_VALUE || input.value == INVALID_HANDLE_VALUE)
            return std::unexpected(Error(ErrorCategory::FileSystem, 180, "Cannot open process log.", GetLastError()));
        SIZE_T size = 0; InitializeProcThreadAttributeList(nullptr, 1, 0, &size); attributes.resize(size);
        auto list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
        if(!InitializeProcThreadAttributeList(list, 1, 0, &size)) return std::unexpected(Error(ErrorCategory::General, 180, "Cannot initialize child handle list.", GetLastError()));
        attributeList.list = list;
        inherited[0] = output.value; inherited[1] = input.value;
        if(!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr))
            return std::unexpected(Error(ErrorCategory::General, 180, "Cannot configure child handles.", GetLastError()));
        startup.lpAttributeList = list; startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput = input.value; startup.StartupInfo.hStdOutput = output.value; startup.StartupInfo.hStdError = output.value;
        flags |= EXTENDED_STARTUPINFO_PRESENT;
    }
    PROCESS_INFORMATION info{};
    if(!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, !log.empty(), flags,
                       nullptr, directory.c_str(), &startup.StartupInfo, &info))
        return std::unexpected(Error(ErrorCategory::General, 180, "Cannot start process.", GetLastError()));
    Process process; process.handle_ = info.hProcess; process.id_ = info.dwProcessId;
    if(ownTree) {
        process.job_ = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{}; limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(!process.job_ || !SetInformationJobObject(process.job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) || !AssignProcessToJobObject(process.job_, process.handle_)) {
            const auto code = GetLastError(); TerminateProcess(process.handle_, 1); CloseHandle(info.hThread);
            return std::unexpected(Error(ErrorCategory::General, 180, "Cannot own build process tree.", code));
        }
        if(ResumeThread(info.hThread) == static_cast<DWORD>(-1)) {
            const auto code = GetLastError(); CloseHandle(info.hThread);
            return std::unexpected(Error(ErrorCategory::General, 180, "Cannot resume build process.", code));
        }
    }
    CloseHandle(info.hThread);
    return process;
}

Result<std::optional<uint32_t>> Process::Poll() const {
    if(!handle_) return std::unexpected(Error(ErrorCategory::General, 180, "Process handle is closed."));
    const auto wait = WaitForSingleObject(handle_, 0);
    if(wait == WAIT_TIMEOUT) return std::optional<uint32_t>{};
    DWORD code = 0;
    if(wait != WAIT_OBJECT_0 || !GetExitCodeProcess(handle_, &code))
        return std::unexpected(Error(ErrorCategory::General, 180, "Cannot query process state.", GetLastError()));
    return std::optional<uint32_t>{code};
}
Result<void> Process::Terminate(uint32_t exitCode) {
    if(!handle_ || !(job_ ? TerminateJobObject(job_, exitCode) : TerminateProcess(handle_, exitCode))) return std::unexpected(Error(ErrorCategory::General, 180, "Cannot stop child process.", GetLastError()));
    return {};
}
Result<void> Process::RequestClose() {
    auto state = Poll(); if(!state) return std::unexpected(state.error());
    if(*state) return {};
    struct Request { DWORD id; bool sent = false; } request{id_};
    EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
        auto& value = *reinterpret_cast<Request*>(parameter); DWORD id = 0; GetWindowThreadProcessId(window, &id);
        // 明示的に非表示で起動したRuntimeにも通常終了を要求できるようにする。
        if(id == value.id) value.sent |= PostMessageW(window, WM_CLOSE, 0, 0) != FALSE;
        return TRUE;
    }, reinterpret_cast<LPARAM>(&request));
    if(!request.sent) {
        auto finished = Poll(); if(!finished) return std::unexpected(finished.error());
        if(!finished->has_value()) return std::unexpected(Error(ErrorCategory::General, 180, "Child window is not ready for a close request."));
    }
    return {};
}
}
