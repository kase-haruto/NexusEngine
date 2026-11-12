#include "StandaloneCommands.h"
#include "StandaloneSession.h"
#include "Editor/Core/EditorWorkspace.h"
#include "Foundation/Utility/String/ConvertString.h"
#include "ThirdParty/imgui/imgui.h"

namespace NexusEngine {
////////////////////////////
// 同じ実行操作をMenuとToolbarへ公開する
////////////////////////////
void RegisterStandaloneCommands(EditorWorkspace& workspace, std::shared_ptr<StandaloneSession> session, ProjectLaunchOptions options) {
    EditorCommandRegistry::Command run{[session,&workspace,options = std::move(options)] {
        workspace.Report(session->RequestStart(options));
    }, [session,&workspace] {
        auto state = session->State();
        return state && !state->building && !state->running && !workspace.Session().IsPlaying() && !workspace.IsProjectOperationActive();
    }};
    // 取消・終了待ちも含めた継続状態を公開し、実行中に別モードへ切り替えさせない。
    run.isChecked = [session] { auto state = session->State(); return state && (state->building || state->running); };
    workspace.Commands().Register("project.runStandalone",std::move(run));
    workspace.Commands().Register("project.stopStandalone", {[session,&workspace] {
        workspace.Report(session->Stop());
    }, [session] { auto state = session->State(); return state && (state->building || state->running) && !state->stopping; }});
    // 完了処理はNewFrame前に実行する。非表示ToolbarからRuntime起動の進行を切り離す。
    workspace.AddFramePoller([session,&workspace] { workspace.Report(session->Poll()); });
    workspace.Toolbar().RegisterSection({"project.runtime",200,12,[session](const auto& text,const auto& commands) {
        auto state = session->State();
        if(state) {
            if(!commands.IsChecked("play.mode.window") && !state->building && !state->running && state->error.empty()) return;
            const char* status = state->stopping ? "project.runtime.stopping" : state->building ? "project.runtime.building" :
                state->running ? "project.runtime.running" : !state->error.empty() ? "project.runtime.failed" : "project.runtime.idle";
            ImGui::TextDisabled("%s",text.Text(status).c_str());
            if(ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(450 * ImGui::GetStyle().FontScaleDpi);
                ImGui::TextWrapped("%s",text.Text("project.runtime.savedLevel").c_str());
                if(state->processId) ImGui::Text("PID: %u",state->processId);
                if(!state->buildLog.empty()) ImGui::TextWrapped("Build: %s",ConvertString(state->buildLog.wstring()).c_str());
                if(!state->runtimeLog.empty()) ImGui::TextWrapped("Runtime: %s",ConvertString(state->runtimeLog.wstring()).c_str());
                if(!state->error.empty()) ImGui::TextWrapped("%s",state->error.c_str());
                ImGui::PopTextWrapPos(); ImGui::EndTooltip();
            }
        }
    }});
}
}
