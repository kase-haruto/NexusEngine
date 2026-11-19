#include "NamedPipeChannel.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <sddl.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#pragma comment(lib, "advapi32.lib")

namespace NexusEngine::IPC {
namespace {
constexpr size_t QueueLimit = 64 * 1024 * 1024;
constexpr DWORD PacketLimit = MaximumPayload + 44;
bool ValidEndpoint(const std::string& endpoint) {
    if(endpoint.empty() || endpoint.size() > 120) {
        return false;
    }
    for(const char c : endpoint) {
        if(!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) {
            return false;
        }
    }
    return true;
}
PSECURITY_DESCRIPTOR UserSecurity() {
    HANDLE token = nullptr;
    if(!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return nullptr;
    }
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<std::byte> bytes(size);
    const bool valid = size && GetTokenInformation(token, TokenUser, bytes.data(), size, &size);
    CloseHandle(token);
    LPWSTR sid = nullptr;
    if(!valid || !ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid, &sid)) {
        return nullptr;
    }
    const std::wstring acl = L"D:P(A;;GA;;;" + std::wstring(sid) + L")";
    LocalFree(sid);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) {
        return nullptr;
    }
    return descriptor;
}
}
struct NamedPipeChannel::State {
    std::mutex mutex;
    std::condition_variable_any wake;
    std::deque<std::vector<std::byte>> outgoing;
    std::deque<Message> incoming;
    size_t outgoingBytes = 0, incomingBytes = 0;
    std::atomic<bool> connected = false;
    std::atomic<uint64_t> generation = 0;
    std::jthread worker;
    void Run(std::stop_token stop, std::string endpoint, bool server);
    void ConnectedLoop(std::stop_token stop, HANDLE pipe);
};
void NamedPipeChannel::State::ConnectedLoop(std::stop_token stop, HANDLE pipe) {
    OVERLAPPED read{}, write{};
    read.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    write.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if(!read.hEvent || !write.hEvent) {
        if(read.hEvent) {
            CloseHandle(read.hEvent);
        }
        if(write.hEvent) {
            CloseHandle(write.hEvent);
        }
        return;
    }
    std::vector<std::byte> readBytes(PacketLimit), writeBytes;
    bool reading = false, writing = false, failed = false;
    ++generation;
    connected = true;
    while(!stop.stop_requested() && !failed) {
        if(!reading) {
            ResetEvent(read.hEvent);
            DWORD count = 0;
            if(ReadFile(pipe, readBytes.data(), PacketLimit, &count, &read)) {
                SetEvent(read.hEvent);
                reading = true;
            } else if(GetLastError() == ERROR_IO_PENDING) {
                reading = true;
            } else {
                failed = true;
            }
        }
        if(reading && WaitForSingleObject(read.hEvent, 0) == WAIT_OBJECT_0) {
            DWORD count = 0;
            if(!GetOverlappedResult(pipe, &read, &count, FALSE)) {
                failed = true;
            } else {
                auto message = Decode(std::span(readBytes).first(count));
                std::lock_guard lock(mutex);
                if(!message || incomingBytes + count > QueueLimit) {
                    failed = true;
                } else {
                    incomingBytes += message->payload.size() + 44;
                    incoming.push_back(std::move(*message));
                }
            }
            reading = false;
        }
        if(!writing) {
            {
                std::lock_guard lock(mutex);
                if(!outgoing.empty()) {
                    writeBytes = std::move(outgoing.front());
                    outgoing.pop_front();
                    outgoingBytes -= writeBytes.size();
                }
            }
            if(!writeBytes.empty()) {
                ResetEvent(write.hEvent);
                DWORD count = 0;
                if(WriteFile(pipe, writeBytes.data(), static_cast<DWORD>(writeBytes.size()), &count, &write)) {
                    SetEvent(write.hEvent);
                    writing = true;
                } else if(GetLastError() == ERROR_IO_PENDING) {
                    writing = true;
                } else {
                    failed = true;
                }
            }
        }
        if(writing && WaitForSingleObject(write.hEvent, 0) == WAIT_OBJECT_0) {
            DWORD count = 0;
            if(!GetOverlappedResult(pipe, &write, &count, FALSE) || count != writeBytes.size()) {
                failed = true;
            }
            writing = false;
            writeBytes.clear();
        }
        // I/O待機は専用workerだけに限定する。UI/World threadには待機handleを渡さない。
        std::unique_lock lock(mutex);
        wake.wait_for(lock, stop, std::chrono::milliseconds(5), [] {
            return false;
        });
    }
    connected = false;
    CancelIoEx(pipe, nullptr);
    DWORD ignored = 0;
    if(reading) {
        GetOverlappedResult(pipe, &read, &ignored, TRUE);
    }
    if(writing) {
        GetOverlappedResult(pipe, &write, &ignored, TRUE);
    }
    CloseHandle(read.hEvent);
    CloseHandle(write.hEvent);
    std::lock_guard lock(mutex);
    outgoing.clear();
    incoming.clear();
    outgoingBytes = incomingBytes = 0;
}
void NamedPipeChannel::State::Run(std::stop_token stop, std::string endpoint, bool server) {
    if(!ValidEndpoint(endpoint)) {
        return;
    }
    const std::wstring path = L"\\\\.\\pipe\\nexus-" + std::wstring(endpoint.begin(), endpoint.end());
    auto* descriptor = server ? UserSecurity() : nullptr;
    if(server && !descriptor) {
        return;
    }
    while(!stop.stop_requested()) {
        HANDLE pipe = INVALID_HANDLE_VALUE;
        if(server) {
            SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
            pipe = CreateNamedPipeW(path.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                1, PacketLimit, PacketLimit, 0, &attributes);
        } else {
            pipe = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        }
        if(pipe != INVALID_HANDLE_VALUE) {
            bool accepted = !server;
            if(server) {
                OVERLAPPED connect{};
                connect.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                if(connect.hEvent) {
                    accepted = ConnectNamedPipe(pipe, &connect) != FALSE;
                    const DWORD error = accepted ? ERROR_SUCCESS : GetLastError();
                    if(error == ERROR_PIPE_CONNECTED) {
                        accepted = true;
                    } else if(error == ERROR_IO_PENDING) {
                        while(!stop.stop_requested() && WaitForSingleObject(connect.hEvent, 20) == WAIT_TIMEOUT) {
                        }
                        DWORD ignored = 0;
                        if(stop.stop_requested()) {
                            CancelIoEx(pipe, &connect);
                        }
                        accepted = GetOverlappedResult(pipe, &connect, &ignored, TRUE) != FALSE;
                    }
                    CloseHandle(connect.hEvent);
                }
            } else {
                DWORD mode = PIPE_READMODE_MESSAGE;
                accepted = SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr) != FALSE;
            }
            if(accepted && !stop.stop_requested()) {
                ConnectedLoop(stop, pipe);
            }
            CloseHandle(pipe);
        }
        std::unique_lock lock(mutex);
        wake.wait_for(lock, stop, std::chrono::milliseconds(50), [] {
            return false;
        });
    }
    if(descriptor) {
        LocalFree(descriptor);
    }
}
NamedPipeChannel::NamedPipeChannel(std::string endpoint, bool server) : state_(std::make_unique<State>()) {
    state_->worker = std::jthread([state = state_.get(), endpoint = std::move(endpoint), server](std::stop_token stop) {
        state->Run(stop, endpoint, server);
    });
}
NamedPipeChannel::~NamedPipeChannel() {
    Close();
}
bool NamedPipeChannel::Send(const Message& message) {
    auto packet = Encode(message);
    if(!packet) {
        return false;
    }
    std::lock_guard lock(state_->mutex);
    if(!state_->connected || state_->outgoingBytes + packet->size() > QueueLimit) {
        return false;
    }
    state_->outgoingBytes += packet->size();
    state_->outgoing.push_back(std::move(*packet));
    state_->wake.notify_all();
    return true;
}
std::optional<Message> NamedPipeChannel::TryReceive() {
    std::lock_guard lock(state_->mutex);
    if(state_->incoming.empty()) {
        return {};
    }
    auto message = std::move(state_->incoming.front());
    state_->incoming.pop_front();
    state_->incomingBytes -= message.payload.size() + 44;
    return message;
}
bool NamedPipeChannel::IsOpen() const noexcept {
    return state_->connected;
}
uint64_t NamedPipeChannel::Generation() const noexcept {
    return state_->generation;
}
void NamedPipeChannel::Close() noexcept {
    state_->worker.request_stop();
    state_->wake.notify_all();
    if(state_->worker.joinable()) {
        state_->worker.join();
    }
}
}
