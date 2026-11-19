#include "DirectoryChangeWatcher.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <array>
#include <cstring>

namespace NexusEngine {
struct DirectoryChangeWatcher::State {
    HANDLE directory = INVALID_HANDLE_VALUE, event = nullptr;
    OVERLAPPED operation{};
    alignas(DWORD) std::array<std::byte, 64 * 1024> buffer{};
    std::filesystem::path root;
    bool pending = false, recursive = true;
    size_t completions = 0;
    bool Arm() {
        ResetEvent(event);
        operation = {};
        operation.hEvent = event;
        pending = ReadDirectoryChangesW(directory, buffer.data(), static_cast<DWORD>(buffer.size()), recursive,
                                        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                            FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE,
                                        nullptr, &operation, nullptr) != FALSE;
        return pending;
    }
};
DirectoryChangeWatcher::DirectoryChangeWatcher() : state_(std::make_unique<State>()) {
}
DirectoryChangeWatcher::~DirectoryChangeWatcher() {
    Stop();
}
Result<void> DirectoryChangeWatcher::Start(const std::filesystem::path &root, bool recursive) {
    Stop();
    std::error_code error;
    state_->root = std::filesystem::canonical(root, error);
    if (error) {
        return std::unexpected(Error(ErrorCategory::FileSystem, 291, error.message()));
    }
    if (state_->root.root_name().native().starts_with(L"\\\\") ||
        GetDriveTypeW(state_->root.root_path().c_str()) == DRIVE_REMOTE) {
        return std::unexpected(
            Error(ErrorCategory::FileSystem, 291, "Network-root monitoring is disabled. Use Refresh."));
    }
    state_->recursive = recursive;
    state_->directory =
        CreateFileW(state_->root.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (state_->directory == INVALID_HANDLE_VALUE) {
        return std::unexpected(Error(ErrorCategory::FileSystem, 291,
                                     "Directory notifications are unavailable: " + std::to_string(GetLastError())));
    }
    state_->event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!state_->event || !state_->Arm()) {
        const auto code = GetLastError();
        Stop();
        return std::unexpected(
            Error(ErrorCategory::FileSystem, 291, "Cannot start directory notifications: " + std::to_string(code)));
    }
    return {};
}
void DirectoryChangeWatcher::Stop() noexcept {
    if (state_->directory != INVALID_HANDLE_VALUE) {
        if (state_->pending) {
            // bufferとOVERLAPPEDの寿命を、cancelの完了まで保持する。
            CancelIoEx(state_->directory, &state_->operation);
            DWORD transferred = 0;
            GetOverlappedResult(state_->directory, &state_->operation, &transferred, TRUE);
        }
        CloseHandle(state_->directory);
    }
    if (state_->event) {
        CloseHandle(state_->event);
    }
    state_->directory = INVALID_HANDLE_VALUE;
    state_->event = nullptr;
    state_->pending = false;
}
bool DirectoryChangeWatcher::Active() const noexcept {
    return state_->pending;
}
size_t DirectoryChangeWatcher::CompletionCount() const noexcept {
    return state_->completions;
}
Result<DirectoryChanges> DirectoryChangeWatcher::Poll() {
    DirectoryChanges result;
    if (!state_->pending || WaitForSingleObject(state_->event, 0) != WAIT_OBJECT_0) {
        return result;
    }
    DWORD bytes = 0;
    if (!GetOverlappedResult(state_->directory, &state_->operation, &bytes, FALSE)) {
        const auto code = GetLastError();
        if (code == ERROR_IO_INCOMPLETE) {
            return result;
        }
        state_->pending = false;
        Stop();
        return std::unexpected(
            Error(ErrorCategory::FileSystem, 291, "Directory notifications stopped: " + std::to_string(code)));
    }
    ++state_->completions;
    state_->pending = false;
    result.overflow = bytes == 0;
    size_t offset = 0;
    while (bytes && offset + offsetof(FILE_NOTIFY_INFORMATION, FileName) <= bytes && result.paths.size() < 2048) {
        const auto *item = reinterpret_cast<const FILE_NOTIFY_INFORMATION *>(state_->buffer.data() + offset);
        const auto header = offsetof(FILE_NOTIFY_INFORMATION, FileName);
        if (item->FileNameLength % sizeof(wchar_t) || offset + header + item->FileNameLength > bytes) {
            result.overflow = true;
            break;
        }
        const std::filesystem::path relative(std::wstring(item->FileName, item->FileNameLength / sizeof(wchar_t)));
        if (!relative.empty() && !relative.is_absolute() && *relative.begin() != L"..") {
            result.paths.push_back(state_->root / relative);
        }
        if (!item->NextEntryOffset) {
            break;
        }
        if (item->NextEntryOffset < header || offset + item->NextEntryOffset <= offset) {
            result.overflow = true;
            break;
        }
        offset += item->NextEntryOffset;
    }
    if (result.paths.size() == 2048) {
        result.overflow = true;
    }
    if (!state_->Arm()) {
        const auto code = GetLastError();
        Stop();
        return std::unexpected(
            Error(ErrorCategory::FileSystem, 291, "Cannot rearm directory notifications: " + std::to_string(code)));
    }
    return result;
}
} // namespace NexusEngine
