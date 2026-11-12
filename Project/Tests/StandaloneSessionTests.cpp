#include "Editor/Services/StandaloneSession.h"
#include "Editor/Services/StandaloneCommands.h"
#include "Editor/Core/EditorWorkspace.h"
#include "Runtime/Project/ProjectService.h"
#include "Platform/FileSystem/AtomicFile.h"
#include "ThirdParty/imgui/imgui.h"
#include <Windows.h>
#include <chrono>
#include <thread>
#include <fstream>
#include <iostream>

using namespace NexusEngine;
namespace {
void Require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
std::string Read(const std::filesystem::path& file) { std::ifstream input(file); return {std::istreambuf_iterator<char>(input),{}}; }
StandaloneState Wait(StandaloneSession& session) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
    for(;;) {
        auto polled = session.Poll(); if(!polled) throw std::runtime_error(polled.error().GetMessageText());
        auto state = session.State(); if(!state) throw std::runtime_error(state.error().GetMessageText());
        if(!state->running && !state->building) return *state;
        if(std::chrono::steady_clock::now() > deadline) throw std::runtime_error("Standalone child did not finish.");
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}
}
int wmain(int argc,wchar_t** argv) {
    if(argc != 3 && !(argc == 4 && std::wstring_view(argv[3]) == L"--with-editor")) { std::cerr << "NexusStandaloneTests <project.nxproject> <engine.nxengine> [--with-editor]\n"; return 1; }
    std::optional<StandaloneSession> active;
    std::shared_ptr<StandaloneSession> commandSession;
    std::optional<Process> liveEditor;
    try {
        auto project = ProjectService::Open(argv[1]); if(!project) throw std::runtime_error(project.error().GetMessageText());
        auto engine = EngineManifest::Load(argv[2]); if(!engine) throw std::runtime_error(engine.error().GetMessageText());
        auto uuid = CreateUuid(); Require(uuid.has_value(),"Cannot create standalone test ID.");
        const auto root = project->Root() / "Build/StandaloneTests" / *uuid;
        std::filesystem::create_directories(root);
        auto context = std::make_shared<const ProjectContext>(std::move(*project));
        std::vector<std::pair<std::filesystem::path,std::filesystem::file_time_type>> dependencyTimes;
        if(argc == 4) {
            auto launched = ProjectBuild::Launch(*context,*engine,ProjectHostKind::Editor,ProjectLaunchOptions{.hidden=true,.log=root / "LiveEditor.log"});
            if(!launched) throw std::runtime_error(launched.error().GetMessageText());
            liveEditor = std::move(*launched);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
            while(Read(root / "LiveEditor.log").find("NexusEngine initialization completed") == std::string::npos) {
                auto state = liveEditor->Poll(); Require(state && !*state,"Live Editor exited before initializing.");
                if(std::chrono::steady_clock::now() > deadline) throw std::runtime_error("Live Editor initialization timed out.");
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            auto targets = ProjectBuild::Targets(*context,engine->configuration);
            Require(targets.has_value(),"Cannot resolve shared output directory.");
            for(const auto& dependency : engine->runtimeFiles) {
                const auto file = targets->runtimeExecutable.parent_path() / dependency.filename();
                dependencyTimes.emplace_back(file,std::filesystem::last_write_time(file));
            }
        }
        // Editorと同じCommand接続を使い、手動のRuntimeビルドなしで実行できることを確認する。
        {
            commandSession = std::make_shared<StandaloneSession>(context,*engine);
            ImGui::CreateContext(); ImGui::GetIO().IniFilename = nullptr;
            struct GuiLifetime { ~GuiLifetime() { ImGui::DestroyContext(); } } guiLifetime;
            EditorSession editorSession;
            Require(editorSession.Create().has_value(),"Cannot create command-test Editor session.");
            EditorWorkspace workspace(editorSession);
            workspace.Initialize(context->Root(),{},engine->data,context.get());
            RegisterStandaloneCommands(workspace,commandSession,{.hidden=true});
            // Project Job中は直接command経由の別Runtime起動も拒否する。compilerは起動しない。
            bool developmentBusy = true;
            EditorCommandRegistry::Command development{[] {}};
            development.isChecked = [&] { return developmentBusy; };
            Require(workspace.Commands().Register("project.development", std::move(development)),"Cannot register operation state probe.");
            Require(!workspace.Commands().Execute("project.runStandalone"),"Project operation allowed a competing Runtime build.");
            developmentBusy = false;
            Require(workspace.Commands().CanExecute("project.runStandalone"),"Editor Runtime command is unavailable.");
            Require(!workspace.Commands().CanExecute("project.stopStandalone"),"Idle Runtime stop command is enabled.");
            Require(workspace.Commands().Execute("play.mode.window"),"Cannot choose separate-window mode.");
            Require(workspace.Commands().Execute("play.start"),"Editor could not request Runtime build and launch.");
            Require(!workspace.Commands().CanExecute("play.mode.editor"),"Mode can change during Runtime startup.");
            Require(!workspace.Commands().CanExecute("project.runStandalone"),"Runtime command permits duplicate build/run.");
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(5);
            bool launched = false;
            for(;;) {
                auto polled = commandSession->Poll(); if(!polled) throw std::runtime_error(polled.error().GetMessageText());
                auto state = commandSession->State(); Require(state.has_value(),"Cannot observe Editor Runtime command.");
                if(state->running) {
                    launched = true;
                    Require(workspace.Commands().CanExecute("project.stopStandalone"),"Running Runtime cannot be stopped from Editor.");
                    if(Read(state->runtimeLog).find("NexusEngine initialization completed") != std::string::npos) {
                        Require(workspace.Commands().Execute("play.stop"),"Editor Runtime stop command failed.");
                        break;
                    }
                }
                if(!state->building && !state->running) throw std::runtime_error("Editor Runtime ended before it initialized.");
                if(std::chrono::steady_clock::now() > deadline) { static_cast<void>(commandSession->Stop()); throw std::runtime_error("Editor build/run command timed out."); }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            Require(launched,"Editor command never launched a Runtime.");
            auto stopped = Wait(*commandSession);
            Require(stopped.exitCode == uint32_t{0},"Editor command did not shut Runtime down normally.");
            Require(Read(stopped.runtimeLog).find("NexusEngine shutdown completed") != std::string::npos,"Editor command bypassed Runtime shutdown.");
            // 開始直後の取消もビルド完了と競合して子Runtimeを起動してはいけない。
            Require(workspace.Commands().Execute("play.start"),"Editor cannot restart Runtime.");
            Require(workspace.Commands().Execute("play.stop"),"Editor cannot cancel Runtime build.");
            auto cancelled = Wait(*commandSession);
            Require(!cancelled.running && cancelled.processId == 0,"Cancelled build launched a Runtime.");
            workspace.Shutdown();
        }
        commandSession.reset();
        {
            auto brokenEngine = *engine; brokenEngine.sdk = root / "Missing SDK";
            StandaloneSession failed(context,std::move(brokenEngine));
            Require(failed.RequestStart({.hidden=true}).has_value(),"Failed build could not be scheduled.");
            bool rejected = false;
            try { static_cast<void>(Wait(failed)); } catch(const std::exception&) { rejected = true; }
            auto state = failed.State();
            Require(rejected && state && !state->error.empty() && !state->running && !state->building,"Failed Runtime build was not exposed as an error.");
            Require(failed.Poll().has_value(),"Runtime build failure was emitted more than once.");
            Require(std::filesystem::is_regular_file(state->buildLog),"Failed Runtime build log is missing.");
        }
        active.emplace(context,*engine);
        Require(active->Stop().has_value(),"Stopping an idle session failed.");
        const auto boundedLog = root / "Bounded.log";
        auto started = active->Start({.hidden=true,.frames=3,.log=boundedLog});
        if(!started) throw std::runtime_error(started.error().GetMessageText());
        auto running = active->State(); Require(running && running->running,"Session did not retain its own live process.");
        Require(!active->Start({.hidden=true}),"Session spawned a duplicate Runtime.");
        auto bounded = Wait(*active); Require(bounded.exitCode == uint32_t{0},"Bounded standalone did not exit successfully.");
        Require(Read(boundedLog).find("PASS: completed 3 GPU frames") != std::string::npos,"Standalone did not complete GPU frames.");
        const auto stopLog = root / "Stopped.log";
        started = active->Start({.hidden=true,.log=stopLog}); if(!started) throw std::runtime_error(started.error().GetMessageText());
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
        while(Read(stopLog).find("NexusEngine initialization completed") == std::string::npos) {
            auto state = active->State(); Require(state && state->running,"Runtime exited before initialization.");
            if(std::chrono::steady_clock::now() > deadline) throw std::runtime_error("Standalone initialization timed out.");
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        Require(active->Stop().has_value(),"Could not request normal termination of hidden Runtime.");
        auto stopped = Wait(*active); Require(stopped.exitCode == uint32_t{0},"Requested close did not exit normally.");
        Require(Read(stopLog).find("NexusEngine shutdown completed") != std::string::npos,"Runtime bypassed normal shutdown.");
        Require(active->Stop().has_value(),"Repeated stop was not idempotent.");
        // Editor側の所有者が消えても、独立Runtimeは自分のframe loopを完了する。
        const auto detachedLog = root / "Detached.log";
        HANDLE observed = nullptr;
        {
            StandaloneSession detached(context,*engine);
            auto launched = detached.Start({.hidden=true,.frames=3,.log=detachedLog}); if(!launched) throw std::runtime_error(launched.error().GetMessageText());
            auto state = detached.State(); Require(state && state->running,"Detached child was not alive.");
            observed = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE,FALSE,state->processId);
            Require(observed != nullptr,"Cannot observe detached Runtime.");
        }
        struct Close { HANDLE value; ~Close() { if(WaitForSingleObject(value,0) == WAIT_TIMEOUT) TerminateProcess(value,99); CloseHandle(value); } } close{observed};
        Require(WaitForSingleObject(observed,45000) == WAIT_OBJECT_0,"Detached Runtime did not complete.");
        DWORD exitCode = 1; Require(GetExitCodeProcess(observed,&exitCode) && exitCode == 0,"Owner destruction killed detached Runtime.");
        Require(Read(detachedLog).find("PASS: completed 3 GPU frames") != std::string::npos,"Detached Runtime did not finish its GPU work.");
        if(liveEditor) {
            for(const auto& [file,time] : dependencyTimes) Require(std::filesystem::last_write_time(file) == time,"Unchanged loaded DLL was rewritten during Runtime build.");
            Require(liveEditor->RequestClose().has_value(),"Cannot close own validation Editor normally.");
            const auto editorCloseDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
            for(;;) {
                auto state = liveEditor->Poll(); Require(state.has_value(),"Cannot observe own validation Editor.");
                if(*state) { Require(**state == 0,"Validation Editor did not exit normally."); break; }
                if(std::chrono::steady_clock::now() > editorCloseDeadline) throw std::runtime_error("Validation Editor close timed out.");
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            std::cout << "PASS: Runtime builds and runs while the real Editor holds shared DXC DLLs; unchanged DLL timestamps are preserved.\n";
        }
        std::cout << "PASS: unified Run/Stop routes to separate Runtime, cancels builds, rejects duplicates, closes normally and preserves independent child lifetime.\n";
        return 0;
    } catch(const std::exception& exception) {
        if(liveEditor) { static_cast<void>(liveEditor->RequestClose()); std::this_thread::sleep_for(std::chrono::milliseconds(200)); auto state = liveEditor->Poll(); if(state && !*state) static_cast<void>(liveEditor->Terminate(99)); }
        for(auto* session : {active ? &*active : nullptr,commandSession.get()}) if(session) {
            static_cast<void>(session->Stop());
            auto state = session->State();
            if(state && state->running) {
                HANDLE owned = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE,FALSE,state->processId);
                if(owned) { if(WaitForSingleObject(owned,5000) == WAIT_TIMEOUT) TerminateProcess(owned,99); CloseHandle(owned); }
            }
        }
        std::cerr << exception.what() << '\n'; return 1;
    }
}
