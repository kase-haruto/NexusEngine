#include "ProjectDevelopmentCommands.h"
#include "Editor/Core/EditorWorkspace.h"
#include "Runtime/Project/ProjectService.h"
#include "Runtime/Project/ProjectJson.h"
#include "Platform/FileSystem/AtomicFile.h"
#include "ThirdParty/imgui/imgui.h"
#include <future>
#include <thread>
#include <algorithm>
#include <stdexcept>
#include <iterator>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace NexusEngine {
namespace {
/*-----------------------------------------------------------------------------------------
 * ProjectDevelopmentJob
 * - 既存Process / ProjectBuildへ作業を委譲し、UIへ値結果だけを返す
 * - Module追加の短いcommitは中断せず、Build / Packageだけ所有Jobを取消可能にする
 *---------------------------------------------------------------------------------------*/
struct ProjectDevelopmentJob {
    std::future<Result<void>> future; //< Worldを参照しないworker
    std::stop_source cancel; //< 自分が開始したProcessだけを停止する
    std::shared_ptr<const ProjectContext> project; //< worker用の値snapshot。開始時に現在の定義を読み直す
    EngineInstallation engine; //< 選択済みEngine。Registryからの自動fallbackは行わない
    ProjectBuildOptions options; //< 通常・Native patchの成果物を区別する
    std::string status = "Idle"; //< UI threadが表示する結果文字列
    std::filesystem::path log; //< 子processごとに新規作成するログ
    ~ProjectDevelopmentJob() { cancel.request_stop(); if(future.valid()) future.wait(); }
    /** \brief 完了を所有Threadで回収する。futureの例外も既存Consoleへ値として通知する */
    void Poll(EditorWorkspace& workspace) {
        if(future.valid() && future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                auto result = future.get(); workspace.Report(result); status = result ? "Completed. Build and restart after changing Modules." : result.error().GetMessageText();
            } catch(const std::exception& exception) { status = exception.what(); workspace.Report(std::unexpected(Error(ErrorCategory::Framework, 246, status))); }
              catch(...) { status = "Unknown project operation failure."; workspace.Report(std::unexpected(Error(ErrorCategory::Framework, 246, status))); }
        }
    }
    /** \brief 描画上の無効化に加えて実行直前にも競合を確認する。Worldはworkerへ渡さない */
    void Start(EditorWorkspace& workspace, std::string action, std::vector<std::wstring> arguments = {}) {
        if(future.valid() || workspace.Session().IsPlaying() || workspace.Commands().IsChecked("project.runStandalone") || workspace.IsProjectOperationActive()) return;
        try {
            auto id = CreateUuid(); if(!id) { status = id.error().GetMessageText(); workspace.Report(std::unexpected(id.error())); return; }
            log = project->Root() / "Build/Logs/Project" / (*id + ".log"); std::filesystem::create_directories(log.parent_path());
            cancel = std::stop_source{}; status = "Working...";
            future = std::async(std::launch::async, [project = project, engine = engine, options = options, action, arguments = std::move(arguments), log = log, stop = cancel.get_token()]() mutable -> Result<void> {
            try {
                auto current = ProjectService::Open(project->File()); if(!current) return std::unexpected(current.error());
                project = std::make_shared<const ProjectContext>(std::move(*current));
                if(action == "Build" || action == "Rebuild" || action == "RebuildScripts") {
                    // Script再構築の要求を通常Buildへ潰さず、追加Managed Moduleまで伝える。
                    // Nativeは差分Buildのままにし、共通の配置・Metadata・stamp確定を通す。
                    options.rebuild = action == "Rebuild";
                    options.rebuildManaged = action == "RebuildScripts";
                    auto result = ProjectBuild::Build(*project, engine, ProjectHostKind::Editor, log, stop, options);
                    if(!result) return std::unexpected(result.error()); return {};
                }
                wchar_t system[32768]{}; if(!GetSystemDirectoryW(system, static_cast<UINT>(std::size(system)))) throw std::runtime_error("Cannot locate PowerShell.");
                std::vector<std::wstring> command{L"-NoProfile", L"-ExecutionPolicy", L"Bypass", L"-File", (engine.sdk / "Tools/Project" / (action + ".ps1")).wstring(), L"-ProjectFile", project->File().wstring(), L"-EngineManifest", engine.manifest.wstring()};
                command.insert(command.end(), arguments.begin(), arguments.end());
                auto process = Process::Start(std::filesystem::path(system) / "WindowsPowerShell/v1.0/powershell.exe", command, project->Root(), true, log, true);
                if(!process) return std::unexpected(process.error());
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(30);
                for(;;) {
                    auto state = process->Poll(); if(!state) return std::unexpected(state.error());
                    if(*state) { if(**state) return std::unexpected(Error(ErrorCategory::Framework, 246, "Project operation failed. Log: " + ProjectJson::Utf8(log))); return {}; }
                    if(action == "Package-Game" && (stop.stop_requested() || std::chrono::steady_clock::now() > deadline)) { static_cast<void>(process->Terminate()); return std::unexpected(Error(ErrorCategory::Framework, 246, "Package cancelled; only its new staging may remain.")); }
                    std::this_thread::sleep_for(std::chrono::milliseconds(30));
                }
            } catch(const std::exception& exception) { return std::unexpected(Error(ErrorCategory::Framework, 246, exception.what())); }
              catch(...) { return std::unexpected(Error(ErrorCategory::Framework, 246, "Unknown project operation failure.")); }
            });
        } catch(const std::exception& exception) { status = exception.what(); workspace.Report(std::unexpected(Error(ErrorCategory::Framework, 246, status))); }
          catch(...) { status = "Cannot start project operation."; workspace.Report(std::unexpected(Error(ErrorCategory::Framework, 246, status))); }
    }
};
/*-----------------------------------------------------------------------------------------
 * ProjectDevelopmentPanel
 * - 言語共通のModule一覧・作成・Build / Package入力を表示する
 * - InspectorやCLRを所有せず、Level / Assetの編集は既存サービスへ残す
 *---------------------------------------------------------------------------------------*/
class ProjectDevelopmentPanel final : public IEditorPanel {
    EditorWorkspace& workspace_; //< Panelより長寿命。開始条件の再検査だけに借用しworkerへ渡さない
    std::shared_ptr<ProjectDevelopmentJob> job_; //< 閉じたPanelでもWorkspaceが完了を回収する共有Job
    char module_[65] = "GameplayExtra", source_[65] = "Controller", dependencies_[512]{}, package_[1024]{}, dotnet_[1024]{}; //< 値入力だけを所有する
    int language_ = 0, selected_ = 0; //< 言語は新Moduleの初期実装方式でありProjectの制約ではない
    char sdk_[1024]{}; //< マシン固有の明示SDK配置。Projectへ保存しない
public:
    ProjectDevelopmentPanel(EditorWorkspace& workspace, std::shared_ptr<ProjectDevelopmentJob> job) : workspace_(workspace), job_(std::move(job)) { const auto path = ProjectJson::Utf8(job_->options.livePPDirectory); if(path.size() < sizeof(sdk_)) std::copy(path.begin(), path.end(), sdk_); }
    std::string_view Id() const noexcept override { return "project.development"; }
    std::string_view TitleKey() const noexcept override { return "Project"; }
    void Draw(const EditorContext& context) override {
        auto project = ProjectService::Open(job_->project->File());
        if(project) for(const auto& item : project->Descriptor().modules) {
            ImGui::TextWrapped("%s (%s) %s", item.id.c_str(), item.language == ProjectModuleLanguage::Cpp ? "C++" : "C#", item.path.c_str());
            for(const auto& dependency : item.dependencies) ImGui::TextDisabled("  requires %s", dependency.c_str());
        }
        ImGui::Separator(); ImGui::TextWrapped("%s", job_->status.c_str());
        if(!job_->log.empty()) ImGui::TextWrapped("Log: %s", ProjectJson::Utf8(job_->log).c_str());
        const bool disabled = context.session.IsPlaying() || context.commands.IsChecked("project.runStandalone") || workspace_.IsProjectOperationActive(); ImGui::BeginDisabled(disabled);
        int profile = job_->options.profile == ProjectBuildProfile::NativeLivePatch ? 1 : 0;
        if(ImGui::Combo("Build profile", &profile, "Standard\0Native Live++\0")) job_->options.profile = profile ? ProjectBuildProfile::NativeLivePatch : ProjectBuildProfile::Standard;
        ImGui::InputText("Live++ SDK", sdk_, sizeof(sdk_)); job_->options.livePPDirectory = profile ? ProjectJson::Path(sdk_) : std::filesystem::path{};
        if(ImGui::Button("Build")) job_->Start(workspace_, "Build"); ImGui::SameLine(); if(ImGui::Button("Rebuild")) job_->Start(workspace_, "Rebuild");
        if(project && std::ranges::any_of(project->Descriptor().modules, [](const auto& item) { return item.language == ProjectModuleLanguage::CSharp; })) {
            ImGui::SameLine(); if(ImGui::Button("Rebuild Scripts")) job_->Start(workspace_, "RebuildScripts");
        }
        ImGui::InputText("Module ID", module_, sizeof(module_)); ImGui::Combo("Initial implementation", &language_, "C++ Native\0C# Managed\0");
        ImGui::InputText("Dependencies (IDs)", dependencies_, sizeof(dependencies_));
        if(ImGui::Button("Add Module")) {
            std::vector<std::wstring> args{L"-ModuleId", ProjectJson::Path(module_).wstring(), L"-Language", language_ == 0 ? L"Cpp" : L"CSharp"};
            if(*dependencies_) { args.push_back(L"-Dependencies"); args.push_back(ProjectJson::Path(dependencies_).wstring()); }
            job_->Start(workspace_, "Add-GameModule", std::move(args));
        }
        if(project && !project->Descriptor().modules.empty()) {
            selected_ = (std::min)(selected_, static_cast<int>(project->Descriptor().modules.size()) - 1);
            const auto& selected = project->Descriptor().modules[static_cast<size_t>(selected_)];
            if(ImGui::BeginCombo("Source Module", selected.id.c_str())) { for(size_t index = 0; index < project->Descriptor().modules.size(); ++index) if(ImGui::Selectable(project->Descriptor().modules[index].id.c_str(), selected_ == static_cast<int>(index))) selected_ = static_cast<int>(index); ImGui::EndCombo(); }
            ImGui::InputText("Source name", source_, sizeof(source_));
            if(ImGui::Button("Create Behaviour / Native System")) job_->Start(workspace_, "Add-GameSource", {L"-ModuleId", ProjectJson::Path(selected.id).wstring(), L"-Name", ProjectJson::Path(source_).wstring()});
        }
        ImGui::InputText("New Package directory", package_, sizeof(package_)); ImGui::InputText("Private .NET root (optional)", dotnet_, sizeof(dotnet_));
        if(ImGui::Button("Package")) { std::vector<std::wstring> args{L"-OutputDirectory", ProjectJson::Path(package_).wstring()}; if(*dotnet_) { args.push_back(L"-DotNetRoot"); args.push_back(ProjectJson::Path(dotnet_).wstring()); } job_->Start(workspace_, "Package-Game", std::move(args)); }
        ImGui::EndDisabled();
        ImGui::TextWrapped("Attach a ScriptComponent and choose the generated Script Type in the common Details panel after Build / restart.");
    }
};
}
void RegisterProjectDevelopmentCommands(EditorWorkspace& workspace, std::shared_ptr<const ProjectContext> project, EngineInstallation engine, ProjectBuildOptions options) {
    auto job = std::make_shared<ProjectDevelopmentJob>(); job->project = std::move(project); job->engine = std::move(engine); job->options = std::move(options);
    workspace.Panels().Register(std::make_unique<ProjectDevelopmentPanel>(workspace, job), false);
    EditorCommandRegistry::Command development{[&workspace] { workspace.Panels().Open("project.development"); }};
    // Panelを開く操作は許可したまま、Job寿命だけを他の開始条件へ公開する。
    development.isChecked = [job] { return job->future.valid(); };
    workspace.Commands().Register("project.development", std::move(development));
    workspace.Menu().RegisterTool({"Project Modules / Build / Package", "project.development"});
    workspace.AddFramePoller([job, &workspace] { job->Poll(workspace); });
    workspace.Toolbar().RegisterSection({"project.operations", 215, 7, [job](const auto&, const auto&) { if(job->future.valid()) ImGui::TextUnformatted("Project operation..."); }});
}
}
