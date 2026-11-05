#include "AtomicFile.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <objbase.h>
#include <array>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <bcrypt.h>
#include <vector>

namespace NexusEngine {
namespace {
Error Failure(const char* operation, DWORD code) { return Error(ErrorCategory::FileSystem, 170, operation, code); }
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() { if(value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
}

////////////////////////////
// マシン固有パスを含まない識別子を発行する
////////////////////////////
Result<std::string> CreateUuid() {
    GUID id{};
    const auto result = CoCreateGuid(&id);
    if(FAILED(result)) return std::unexpected(Failure("Cannot create project UUID.", static_cast<DWORD>(result)));
    std::array<wchar_t, 40> text{};
    if(StringFromGUID2(id, text.data(), static_cast<int>(text.size())) == 0)
        return std::unexpected(Failure("Cannot format project UUID.", ERROR_INVALID_DATA));
    std::string value;
    for(size_t index = 1; index < 37; ++index) {
        char ch = static_cast<char>(text[index]);
        value += ch >= 'A' && ch <= 'F' ? static_cast<char>(ch + ('a' - 'A')) : ch;
    }
    return value;
}

////////////////////////////
// 書込途中のデータを既存ファイルへ公開しない
////////////////////////////
Result<void> WriteFileAtomically(const std::filesystem::path& file, std::string_view bytes, bool replace) {
    auto id = CreateUuid();
    if(!id) return std::unexpected(id.error());
    const auto temporary = file.parent_path() / (".nexus-" + *id + ".tmp");
    DWORD failure = ERROR_SUCCESS;
    {
        Handle output{CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
        if(output.value == INVALID_HANDLE_VALUE) return std::unexpected(Failure("Cannot create temporary project file.", GetLastError()));
        DWORD written = 0;
        if(bytes.size() > MAXDWORD) failure = ERROR_FILE_TOO_LARGE;
        else if(!WriteFile(output.value, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr)) failure = GetLastError();
        else if(written != bytes.size()) failure = ERROR_WRITE_FAULT;
        else if(!FlushFileBuffers(output.value)) failure = GetLastError();
    }
    if(failure == ERROR_SUCCESS && !MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0))) failure = GetLastError();
    if(failure != ERROR_SUCCESS) {
        DeleteFileW(temporary.c_str());
        return std::unexpected(Failure("Cannot publish project file; previous file was retained.", failure));
    }
    return {};
}

////////////////////////////
// Registryのread-modify-writeをパス単位で排他する
////////////////////////////
Result<void> WithFileLock(const std::filesystem::path& file, const std::function<Result<void>()>& update) {
    std::error_code error;
    auto absolute = std::filesystem::absolute(file, error);
    if(error) return std::unexpected(Failure("Cannot resolve registry lock path.", static_cast<DWORD>(error.value())));
    auto normalized = std::filesystem::weakly_canonical(absolute, error).wstring();
    if(error) return std::unexpected(Failure("Cannot resolve registry lock path.", static_cast<DWORD>(error.value())));
    CharUpperBuffW(normalized.data(), static_cast<DWORD>(normalized.size()));
    uint64_t hash = 14695981039346656037ull;
    for(wchar_t ch : normalized) { hash ^= static_cast<uint16_t>(ch); hash *= 1099511628211ull; }
    std::wostringstream name; name << L"Local\\NexusProjectRegistry_" << std::hex << hash;
    Handle mutex{CreateMutexW(nullptr, FALSE, name.str().c_str())};
    if(!mutex.value) return std::unexpected(Failure("Cannot create registry lock.", GetLastError()));
    const auto wait = WaitForSingleObject(mutex.value, 5000);
    if(wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) return std::unexpected(Failure("Cannot acquire registry lock.", wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError()));
    struct Release { HANDLE handle; ~Release() { ReleaseMutex(handle); } } release{mutex.value};
    return update();
}

bool SameFilePath(const std::filesystem::path& first, const std::filesystem::path& second) {
    // JSONの汎用区切りとWin32の区切りを揃えてから比較する。
    auto left = first.lexically_normal().make_preferred();
    auto right = second.lexically_normal().make_preferred();
    if(!left.empty() && left.filename().empty() && left != left.root_path()) left = left.parent_path();
    if(!right.empty() && right.filename().empty() && right != right.root_path()) right = right.parent_path();
    return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
}

Result<void> PublishDirectory(const std::filesystem::path& staging, const std::filesystem::path& destination) {
    if(!MoveFileExW(staging.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH))
        return std::unexpected(Failure("Cannot publish Project; destination was not overwritten.", GetLastError()));
    return {};
}

Result<std::string> Sha256File(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    if(!input) return std::unexpected(Failure("Cannot read inventory file.", ERROR_FILE_NOT_FOUND));
    struct Provider { BCRYPT_ALG_HANDLE handle = nullptr; ~Provider() { if(handle) BCryptCloseAlgorithmProvider(handle, 0); } } provider;
    if(BCryptOpenAlgorithmProvider(&provider.handle, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        return std::unexpected(Failure("Cannot initialize SHA-256.", ERROR_NOT_SUPPORTED));
    DWORD size = 0, returned = 0;
    if(BCryptGetProperty(provider.handle, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&size), sizeof(size), &returned, 0) < 0)
        return std::unexpected(Failure("Cannot query SHA-256 storage.", ERROR_INVALID_DATA));
    std::vector<unsigned char> object(size);
    struct Hash { BCRYPT_HASH_HANDLE handle = nullptr; ~Hash() { if(handle) BCryptDestroyHash(handle); } } hash;
    if(BCryptCreateHash(provider.handle, &hash.handle, object.data(), size, nullptr, 0, 0) < 0)
        return std::unexpected(Failure("Cannot create SHA-256 state.", ERROR_INVALID_DATA));
    std::array<unsigned char, 65536> buffer{};
    while(input) {
        input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if(count && BCryptHashData(hash.handle, buffer.data(), static_cast<ULONG>(count), 0) < 0)
            return std::unexpected(Failure("Cannot hash inventory data.", ERROR_INVALID_DATA));
    }
    if(input.bad()) return std::unexpected(Failure("Cannot read complete inventory data.", ERROR_READ_FAULT));
    std::array<unsigned char, 32> digest{};
    if(BCryptFinishHash(hash.handle, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0)
        return std::unexpected(Failure("Cannot finish SHA-256.", ERROR_INVALID_DATA));
    std::ostringstream text; text << std::hex << std::setfill('0');
    for(auto byte : digest) text << std::setw(2) << static_cast<unsigned>(byte);
    return text.str();
}
}
