#include "ManagedReloadCommands.h"
#include "Editor/Core/EditorWorkspace.h"
#include "Platform/Process/Process.h"
#include "Platform/FileSystem/AtomicFile.h"
#include "Runtime/Application/ApplicationPaths.h"
#include "Foundation/Logging/Logger.h"
#include "Runtime/Project/ProjectJson.h"
#include "ThirdParty/imgui/imgui.h"
#include <chrono>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace NexusEngine {
namespace {
/*-----------------------------------------------------------------------------------------
 * ManagedReloadBuild
 * - ユーザーのReload操作でGameScriptsだけを別process・別世代DirectoryへCompileする
 * - 子processを所有し、完了後のRuntime適用はEditorのframe境界だけで行う
 *---------------------------------------------------------------------------------------*/
class ManagedReloadBuild final {
public:
    enum class State { Idle, Compiling, Reloading, Succeeded, CompileError, CompatibilityError, StateRestoreError, Cancelled };
    State status = State::Idle; //< processとRuntime適用結果を区別する値通知。Managed objectは保持しない
    std::shared_ptr<const ProjectContext> project; //< Project / Engineの選択は起動ホストから受け取る
    EngineInstallation engine; //< 検証済みSDKとBuild構成。別Engineへのfallbackは行わない
    CSharpRuntimeSettings settings; //< 稼働中Bootstrap / APIの明示配置。CLRは所有しない
    std::optional<Process> process; //< ownTree付き。Workspace破棄で自分のCompileだけを中止する
    std::filesystem::path candidate, log; //< 世代別出力と新規ログ。稼働中DLLを置換しない
    uint64_t epoch = 0; //< Stop / 再Playを跨いだ自動適用を拒否する
    std::chrono::steady_clock::time_point deadline; //< 無限待機でReload操作を固定しない
    ~ManagedReloadBuild() { try { if(process) static_cast<void>(process->Terminate()); } catch(...) {} }
    /** \brief 成果物を上書きしないCompileを開始する。Source自体や安定APIは変更しない */
    Result<void> Start(EditorWorkspace& workspace) {
        if(process || !workspace.Session().IsPlaying() || workspace.IsProjectOperationActive() || workspace.Commands().IsChecked("project.runStandalone"))
            return std::unexpected(Error(ErrorCategory::Framework, 235, "Reload requires Play and idle project operations."));
        status = State::CompileError;
        auto uuid = CreateUuid(); if(!uuid) return std::unexpected(uuid.error());
        auto directory = ResolveProjectPath(project->Root(), std::filesystem::path("Intermediate/ManagedReload") / *uuid);
        if(!directory) return std::unexpected(directory.error());
        std::error_code error; std::filesystem::create_directories(*directory, error);
        if(error) return std::unexpected(Error(ErrorCategory::FileSystem, 235, error.message()));
        candidate = *directory / "Managed/GameScripts.dll"; log = *directory / "compile.log";
        const auto bootstrap = settings.bootstrapAssembly.parent_path();
        const auto script = engine.sdk / "Tools/Project/Build-Scripts.ps1";
        wchar_t system[32768]{};
        const auto length = GetSystemDirectoryW(system, static_cast<UINT>(std::size(system)));
        if(!length || length >= std::size(system)) return std::unexpected(Error(ErrorCategory::Framework, 235, "Cannot locate Windows PowerShell."));
        const auto powershell = std::filesystem::path(system) / "WindowsPowerShell/v1.0/powershell.exe";
        auto child = Process::Start(powershell, {L"-NoProfile", L"-ExecutionPolicy", L"Bypass", L"-File", script.wstring(),
            L"-ScriptsProject", (project->Root() / "GameScripts.csproj").wstring(), L"-ApiAssembly", (bootstrap / "Nexus.Managed.dll").wstring(),
            L"-MetadataTool", (bootstrap.parent_path() / (settings.monoRoot.empty() ? "ManagedTools/Nexus.Managed.Metadata.dll" : "ManagedTools/Nexus.Managed.Metadata.exe")).wstring(),
            L"-OutputDirectory", candidate.parent_path().wstring(), L"-Configuration", std::wstring(engine.configuration.begin(), engine.configuration.end()), L"-ProjectFile", project->File().wstring()},
            project->Root(), true, log, true);
        if(!child) return std::unexpected(child.error());
        process = std::move(*child); epoch = workspace.Session().PlayEpoch();
        status = State::Compiling;
        deadline = std::chrono::steady_clock::now() + std::chrono::minutes(10);
        NEXUS_LOG_INFO("ManagedReload", "Compiling GameScripts. Running assembly is unchanged until successful compilation."); return {};
    }
    /** \brief 非同期Compileの成功とPlay世代一致を確認後、一度だけReloadを適用する */
    Result<void> Poll(EditorWorkspace& workspace) {
        if(!process) return {};
        if(!workspace.Session().IsPlaying() || workspace.Session().PlayEpoch() != epoch) {
            // Stop / 再Play後は旧Compileを続けず、候補の適用権限と子processを同時に捨てる。
            status = State::Cancelled; static_cast<void>(process->Terminate()); process.reset();
            return std::unexpected(Error(ErrorCategory::Framework, 235, "Managed Reload cancelled because Play changed. Candidate was not applied."));
        }
        if(std::chrono::steady_clock::now() > deadline) {
            static_cast<void>(process->Terminate()); process.reset();
            status = State::CompileError;
            return std::unexpected(Error(ErrorCategory::Framework, 235, "Script compilation timed out; running assembly retained."));
        }
        auto state = process->Poll(); if(!state) { status = State::CompileError; process.reset(); return std::unexpected(state.error()); }
        if(!state->has_value()) return {};
        const auto code = **state; process.reset();
        status = State::CompileError;
        if(code != 0) return std::unexpected(Error(ErrorCategory::Framework, 235, "GameScripts compilation failed; running assembly retained. Log: " + ProjectJson::Utf8(log)));
        if(!workspace.Session().IsPlaying() || workspace.Session().PlayEpoch() != epoch)
            return std::unexpected(Error(ErrorCategory::Framework, 235, "Play changed during Compile. Candidate was not applied."));
        status = State::Reloading;
        ManagedReloadState outcome = ManagedReloadState::CompatibilityError;
        auto result = workspace.ReloadCSharp(candidate, &outcome);
        status = result ? State::Succeeded : outcome == ManagedReloadState::StateRestoreError ? State::StateRestoreError : State::CompatibilityError;
        return result;
    }
    /** \brief 描画専用の値表示。Compileの監視やRuntime変更をこの関数で行わない */
    const char* Text() const noexcept {
        switch(status) {
            case State::Idle: return "Idle";
            case State::Compiling: return "Compiling";
            case State::Reloading: return "Reloading";
            case State::Succeeded: return "Reload Success";
            case State::CompileError: return "Compile Error";
            case State::CompatibilityError: return "Compatibility Error";
            case State::StateRestoreError: return "State Restore Error";
            case State::Cancelled: return "Cancelled";
        }
        return "Unknown";
    }
};
}
void RegisterManagedReloadCommands(EditorWorkspace& workspace, std::shared_ptr<const ProjectContext> project, EngineInstallation engine, CSharpRuntimeSettings settings) {
    if(!project || settings.gameAssembly.empty()) return;
    auto build = std::make_shared<ManagedReloadBuild>(); build->project = std::move(project); build->engine = std::move(engine); build->settings = std::move(settings);
    EditorCommandRegistry::Command reload{[build, &workspace] { workspace.Report(build->Start(workspace)); },
        [build, &workspace] { return workspace.Session().IsPlaying() && !build->process && !workspace.IsProjectOperationActive() && !workspace.Commands().IsChecked("project.runStandalone"); }};
    // Stable IDの状態照会だけを共有し、他の機能へProcessやManaged参照を公開しない。
    reload.isChecked = [build] { return build->process.has_value() || build->status == ManagedReloadBuild::State::Reloading; };
    workspace.Commands().Register("project.reloadCSharp", std::move(reload));
    workspace.Menu().RegisterTool({"Reload C#", "project.reloadCSharp"});
    workspace.Toolbar().RegisterButton("project.reloadCSharp", "Reload C#", "project.reloadCSharp", 130);
    workspace.SetReloadPoller([build, &workspace] { workspace.Report(build->Poll(workspace)); });
    workspace.Toolbar().RegisterSection({"project.managedCompile", 210, 7, [build](const auto&, const auto&) {
        ImGui::TextDisabled("Managed: %s", build->Text());
    }});
}
}
