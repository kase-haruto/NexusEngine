#include "SourceCodeEditor.h"
#include "Platform/FileSystem/AtomicFile.h"
#include "Runtime/Application/ApplicationPaths.h"
#include "Foundation/Utility/String/ConvertString.h"
#include <Windows.h>
#include <shellapi.h>
#include <objbase.h>
#pragma comment(lib, "shell32.lib")
#include <chrono>
#include <cstdlib>
#include <cwctype>
#include <fstream>
#include <thread>

namespace NexusEngine {
namespace {
Error Failed(std::string message) { return Error(ErrorCategory::Framework,206,std::move(message)); }
}
////////////////////////////
// GameのSolutionを解決する
////////////////////////////
Result<std::filesystem::path> SourceCodeEditor::Solution(const ProjectContext& project) {
    std::error_code error;
    std::filesystem::path selected;
    std::filesystem::directory_iterator files(project.Root(),error);
    if(error) return std::unexpected(Failed("Cannot list the project's solution files: " + error.message()));
    for(;files != std::filesystem::directory_iterator{};files.increment(error)) {
        if(error) break;
        const auto& file = *files;
        auto extension = file.path().extension().wstring();
        for(auto& character : extension) character = static_cast<wchar_t>(towlower(character));
        if(extension != L".slnx" && extension != L".sln") continue;
        auto resolved = ResolveProjectPath(project.Root(),file.path());
        if(!resolved) return std::unexpected(resolved.error());
        const bool regular = std::filesystem::is_regular_file(*resolved,error);
        if(error || !regular) return std::unexpected(Failed("The project's solution is not a readable regular file."));
        if(!selected.empty()) return std::unexpected(Failed("Multiple solutions exist in the game root. Keep one game solution there to select it unambiguously."));
        selected = *resolved;
    }
    if(error) return std::unexpected(Failed("Cannot finish listing the project's solutions: " + error.message()));
    if(selected.empty()) return std::unexpected(Failed("No .slnx or .sln exists in this game root. Create a C++ project from a template first."));
    return selected;
}
////////////////////////////
// Visual Studioのインストール先を照会する
////////////////////////////
Result<std::filesystem::path> SourceCodeEditor::VisualStudio(const ProjectContext& project) {
    wchar_t* directory = nullptr; size_t count = 0;
    _wdupenv_s(&directory,&count,L"ProgramFiles(x86)");
    const auto installer = directory ? std::filesystem::path(directory) : std::filesystem::path{};
    std::free(directory);
    if(installer.empty()) return std::unexpected(Failed("Cannot locate Visual Studio Installer (ProgramFiles(x86) is unset)."));
    const auto locator = installer / "Microsoft Visual Studio/Installer/vswhere.exe";
    std::error_code error;
    if(!std::filesystem::is_regular_file(locator,error))
        return std::unexpected(Failed("Visual Studio Installer/vswhere.exe was not found. Install Visual Studio with Desktop development with C++."));
    auto uuid = CreateUuid(); if(!uuid) return std::unexpected(uuid.error());
    // Descriptorの共有Directoryと混ざらない、専用の生成ログだけを書き込む。
    auto log = ResolveProjectPath(project.Root(),std::filesystem::path("Build/Logs/IDE") / (*uuid + ".log"));
    if(!log) return std::unexpected(log.error());
    std::filesystem::create_directories(log->parent_path(),error);
    if(error) return std::unexpected(Failed("Cannot create IDE discovery log directory: " + error.message()));
    // vswhereの既定製品群はIDEだけ。Build Toolsの新しい配置をIDEとして選ばない。
    auto query = Process::Start(locator,{L"-latest",L"-requires",
        L"Microsoft.VisualStudio.Component.VC.Tools.x86.x64",L"-property",L"productPath",L"-utf8"},project.Root(),true,*log,true);
    if(!query) return std::unexpected(query.error());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for(;;) {
        auto state = query->Poll(); if(!state) return std::unexpected(state.error());
        if(*state) {
            if(**state != 0) return std::unexpected(Failed("Visual Studio discovery failed. See the game's Build/Logs/IDE directory."));
            break;
        }
        if(std::chrono::steady_clock::now() >= deadline)
            return std::unexpected(Failed("Visual Studio discovery timed out. Its owned query process will be stopped."));
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const auto size = std::filesystem::file_size(*log,error);
    if(error || size > 65536) return std::unexpected(Failed("Invalid Visual Studio discovery output."));
    std::ifstream input(*log,std::ios::binary);
    std::string path{std::istreambuf_iterator<char>(input),{}};
    if(input.bad()) return std::unexpected(Failed("Cannot read Visual Studio discovery output."));
    if(path.starts_with("\xEF\xBB\xBF")) path.erase(0,3);
    while(!path.empty() && (path.back() == '\r' || path.back() == '\n')) path.pop_back();
    if(path.empty() || path.find_first_of("\r\n\0",0,3) != std::string::npos)
        return std::unexpected(Failed("No single Visual Studio IDE with C++ tools was found. Build Tools alone cannot open a solution."));
    const std::filesystem::path executable{ConvertString(path)};
    if(!executable.is_absolute() || !std::filesystem::is_regular_file(executable,error))
        return std::unexpected(Failed("The discovered Visual Studio executable does not exist."));
    return executable;
}
////////////////////////////
// 選択GameのSolutionをIDEへ渡す
////////////////////////////
Result<Process> SourceCodeEditor::Open(const ProjectContext& project, const std::filesystem::path& executable) {
    auto solution = Solution(project); if(!solution) return std::unexpected(solution.error());
    auto ide = executable.empty() ? VisualStudio(project) : Result<std::filesystem::path>{executable};
    if(!ide) return std::unexpected(ide.error());
    // shellの文字列組立を避け、空白や日本語を含むSolutionも一つのargvとして渡す。
    return Process::Start(*ide,{solution->wstring()},project.Root());
}
Result<void> SourceCodeEditor::OpenFile(const std::filesystem::path& file) {
    std::error_code error;
    const auto resolved = std::filesystem::canonical(file, error);
    if(error || !std::filesystem::is_regular_file(resolved, error) || error)
        return std::unexpected(Failed("The source file no longer exists."));
    auto extension = resolved.extension().wstring();
    for(auto& value : extension) value = static_cast<wchar_t>(std::towlower(value));
    if(extension != L".cs" && extension != L".cpp" && extension != L".cc" && extension != L".cxx" &&
       extension != L".h" && extension != L".hpp" && extension != L".hxx")
        return std::unexpected(Failed("Select a C# or C++ source file."));
    // 引数列をshell文字列へ連結せず、Windowsのファイル関連付けを使用する。
    SHELLEXECUTEINFOW open{};
    open.cbSize = sizeof(open); open.fMask = SEE_MASK_FLAG_NO_UI;
    open.lpVerb = L"open"; open.lpFile = resolved.c_str(); open.nShow = SW_SHOWNORMAL;
    // COMベースの関連付けhandlerにも対応する。既に別apartmentなら既存の初期化を借用する。
    const auto apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool opened = ShellExecuteExW(&open) != FALSE;
    const auto code = opened ? ERROR_SUCCESS : GetLastError();
    if(SUCCEEDED(apartment)) CoUninitialize();
    if(opened) return {};
    if(code != ERROR_NO_ASSOCIATION)
        return std::unexpected(Failed("Cannot open source file in its associated editor. Windows error " + std::to_string(code)));
    // 関連付けのない開発環境でも編集できるよう、System directoryのEditorへ一つのargvで渡す。
    wchar_t system[MAX_PATH]{};
    const auto length = GetSystemDirectoryW(system, MAX_PATH);
    if(!length || length >= MAX_PATH) return std::unexpected(Failed("Cannot locate the Windows text editor."));
    auto process = Process::Start(std::filesystem::path(system) / "notepad.exe", {resolved.wstring()}, resolved.parent_path());
    if(!process) return std::unexpected(process.error());
    return {};
}
}
