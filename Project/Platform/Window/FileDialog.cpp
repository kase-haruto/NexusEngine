#include "FileDialog.h"
#include <ShObjIdl.h>
#include <wrl/client.h>

namespace NexusEngine {
namespace {
Result<std::optional<std::filesystem::path>> Choose(HWND owner, bool folder, bool engine, bool recovery = false) {
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    HRESULT result = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if(FAILED(result)) return std::unexpected(Error(ErrorCategory::Window, 200, "Cannot initialize file picker.", result));
    DWORD flags = 0; result = dialog->GetOptions(&flags);
    if(SUCCEEDED(result)) result = dialog->SetOptions(flags | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | (folder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST));
    if(!folder && SUCCEEDED(result)) {
        const COMDLG_FILTERSPEC filter{recovery ? L"Nexus Migration Recovery" : engine ? L"Nexus Engine" : L"Nexus Project", recovery ? L"Recovery.json" : engine ? L"*.nxengine" : L"*.nxproject"};
        result = dialog->SetFileTypes(1, &filter);
    }
    if(SUCCEEDED(result)) result = dialog->Show(owner);
    if(result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return std::optional<std::filesystem::path>{};
    if(FAILED(result)) return std::unexpected(Error(ErrorCategory::Window, 200, "File picker failed.", result));
    Microsoft::WRL::ComPtr<IShellItem> item;
    result = dialog->GetResult(&item);
    PWSTR name = nullptr;
    if(SUCCEEDED(result)) result = item->GetDisplayName(SIGDN_FILESYSPATH, &name);
    if(FAILED(result)) return std::unexpected(Error(ErrorCategory::Window, 200, "Cannot read selected path.", result));
    const std::filesystem::path path{name}; CoTaskMemFree(name);
    return std::optional<std::filesystem::path>{path};
}
}
Result<std::optional<std::filesystem::path>> FileDialog::Open(HWND owner, bool engineManifest) { return Choose(owner, false, engineManifest); }
Result<std::optional<std::filesystem::path>> FileDialog::Folder(HWND owner) { return Choose(owner, true, false); }
Result<std::optional<std::filesystem::path>> FileDialog::Recovery(HWND owner) { return Choose(owner, false, false, true); }
}
