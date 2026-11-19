#include "ContentBrowserActions.h"
#include <algorithm>
#include "Platform/FileSystem/AtomicFile.h"
#include "Foundation/Utility/String/ConvertString.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <wrl/client.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace NexusEngine {
Result<void> ContentBrowserActions::CreateFolder(AssetCatalog &catalog, const std::filesystem::path &parent,
                                                 std::string_view name) {
    if (name.empty() || name == "." || name == ".." || name.starts_with(".nexus") || name.back() == '.' ||
        name.back() == ' ' ||
        std::ranges::any_of(name,
                            [](unsigned char c) {
                                return c < 32;
                            }) ||
        name.find_first_of("/\\:*?\"<>|") != std::string_view::npos) {
        return std::unexpected(
            Error(ErrorCategory::FileSystem, 281, "Enter a single folder name without path separators."));
    }
    const auto relative = (parent / ConvertString(std::string(name))).lexically_relative(catalog.Root());
    const auto target = ResolveFileWithinRoot(catalog.Root(), AssetCatalog::Utf8(relative));
    if (!target) {
        return std::unexpected(target.error());
    }
    std::error_code error;
    if (!std::filesystem::create_directory(*target, error)) {
        return std::unexpected(
            Error(ErrorCategory::FileSystem, 281, error ? error.message() : "The folder already exists."));
    }
    catalog.Refresh();
    return {};
}
Result<void> ContentBrowserActions::ShowInExplorer(const std::filesystem::path &file) {
    std::error_code error;
    const auto canonical = std::filesystem::canonical(file, error);
    if (error) {
        return std::unexpected(Error(ErrorCategory::FileSystem, 282, error.message()));
    }
    // PIDLを使い、空白・日本語・引用符をshellコマンド文字列へ連結しない。
    const auto apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    PIDLIST_ABSOLUTE id = ILCreateFromPathW(canonical.c_str());
    const auto result = id ? SHOpenFolderAndSelectItems(id, 0, nullptr, 0) : E_FAIL;
    if (id) {
        ILFree(id);
    }
    if (SUCCEEDED(apartment)) {
        CoUninitialize();
    }
    if (FAILED(result)) {
        return std::unexpected(Error(ErrorCategory::FileSystem, 282, "Explorer could not show the selected path."));
    }
    return {};
}
Result<std::vector<std::filesystem::path>> ContentBrowserActions::ChooseImportFiles() {
    const auto apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    std::vector<std::filesystem::path> files;
    auto result = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (SUCCEEDED(result)) {
        dialog->SetOptions(FOS_ALLOWMULTISELECT | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM);
        result = dialog->Show(nullptr);
        Microsoft::WRL::ComPtr<IShellItemArray> items;
        if (SUCCEEDED(result) && SUCCEEDED(dialog->GetResults(&items))) {
            DWORD count = 0;
            items->GetCount(&count);
            for (DWORD index = 0; index < count && index < 256; ++index) {
                Microsoft::WRL::ComPtr<IShellItem> item;
                PWSTR path = nullptr;
                if (SUCCEEDED(items->GetItemAt(index, &item)) &&
                    SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    files.emplace_back(path);
                    CoTaskMemFree(path);
                }
            }
        }
    }
    if (SUCCEEDED(apartment)) {
        CoUninitialize();
    }
    if (FAILED(result) && result != HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        return std::unexpected(Error(ErrorCategory::Resource, 282, "Cannot open import file dialog."));
    }
    return files;
}
} // namespace NexusEngine
