#include "Tools/Project/SourceCodeEditor.h"
#include "Runtime/Project/ProjectService.h"
#include "Platform/FileSystem/AtomicFile.h"
#include "Foundation/Utility/String/ConvertString.h"
#include <Windows.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

using namespace NexusEngine;
namespace {
void Require(bool valid,const char* message) { if(!valid) throw std::runtime_error(message); }
std::filesystem::path Executable() {
    wchar_t path[32768]{};
    const auto size = GetModuleFileNameW(nullptr,path,static_cast<DWORD>(std::size(path)));
    Require(size && size < std::size(path),"Cannot locate IDE test executable."); return path;
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        // IDEを起動せず、実際に渡されたSolutionとCWDを別プロセスで検証する。
        if(argc == 2 && std::filesystem::path(argv[1]).extension() == L".slnx") {
            const std::filesystem::path solution{argv[1]};
            Require(solution.is_absolute(),"IDE did not receive an absolute solution.");
            Require(std::filesystem::equivalent(solution.parent_path(),std::filesystem::current_path()),"IDE working directory is not the game root.");
            std::ofstream output(solution.parent_path() / "IdeRequest.txt",std::ios::binary);
            output << ConvertString(solution.wstring()); return output ? 0 : 2;
        }
        auto uuid = CreateUuid(); Require(uuid.has_value(),"Cannot create source editor test ID.");
        const auto root = Executable().parent_path() / "Validation" / *uuid / L"日本語 Game Folder";
        std::filesystem::create_directories(root.parent_path());
        ProjectDescriptor descriptor; descriptor.name = "Source Editor Test";
        auto project = ProjectService::Create(root,descriptor,[](const ProjectContext& context) -> Result<void> {
            return WriteFileAtomically(context.StartupLevel(),"NEXUS_LEVEL 12\n",false);
        });
        if(!project) throw std::runtime_error(project.error().GetMessageText());
        Require(!SourceCodeEditor::Solution(*project),"Missing solution was accepted.");
        const auto solution = root / L"日本語 Game.slnx";
        { std::ofstream output(solution); output << "<Solution />"; }
        auto selected = SourceCodeEditor::Solution(*project);
        Require(selected && std::filesystem::equivalent(*selected,solution),"Game solution could not be resolved.");
        auto launched = SourceCodeEditor::Open(*project,Executable());
        if(!launched) throw std::runtime_error(launched.error().GetMessageText());
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        for(;;) {
            auto state = launched->Poll(); Require(state.has_value(),"Cannot observe IDE probe.");
            if(*state) { Require(**state == 0,"IDE probe rejected its arguments."); break; }
            if(std::chrono::steady_clock::now() > deadline) { static_cast<void>(launched->Terminate()); throw std::runtime_error("IDE probe timed out."); }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::ifstream input(root / "IdeRequest.txt",std::ios::binary);
        const std::string argument{std::istreambuf_iterator<char>(input),{}};
        Require(argument == ConvertString(solution.wstring()),"Unicode solution argument changed.");
        { std::ofstream output(root / "Another.sln"); output << "fixture"; }
        Require(!SourceCodeEditor::Solution(*project),"Ambiguous game solutions were accepted.");
        // 実環境のインストール検出だけを行う。Visual StudioのWindowは開かない。
        auto ide = SourceCodeEditor::VisualStudio(*project);
        if(!ide) throw std::runtime_error(ide.error().GetMessageText());
        Require(ide->filename() == L"devenv.exe","Discovery did not select a Visual Studio IDE.");
        std::cout << "PASS: game solution discovery, missing/ambiguous rejection, Unicode argv, game CWD, and installed Visual Studio discovery without opening its UI.\n";
        return 0;
    } catch(const std::exception& exception) { std::cerr << exception.what() << '\n'; return 1; }
}
