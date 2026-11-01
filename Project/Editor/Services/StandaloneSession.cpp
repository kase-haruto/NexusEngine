#include "StandaloneSession.h"
#include "Platform/FileSystem/AtomicFile.h"
#include "Runtime/Application/ApplicationPaths.h"
#include "Foundation/Utility/String/ConvertString.h"

namespace NexusEngine {
namespace {
Error Failed(std::string message) { return Error(ErrorCategory::Framework,205,std::move(message)); }
}
StandaloneSession::~StandaloneSession() {
    // 終了中にworkerがRuntimeを起動しない。既に動いている独立Runtimeの寿命は変えない。
    cancelBuild_.request_stop();
    if(build_.valid()) build_.wait();
}
////////////////////////////
// 子プロセスの実際の状態を照会する
////////////////////////////
Result<StandaloneState> StandaloneSession::State() const {
    StandaloneState result;
    result.building = build_.valid(); result.stopping = result.building && cancelBuild_.stop_requested();
    result.buildLog = buildLog_; result.runtimeLog = runtimeLog_; result.error = error_;
    if(child_) {
        auto state = child_->Poll(); if(!state) return std::unexpected(state.error());
        result.running = !state->has_value(); result.processId = child_->Id(); result.exitCode = *state;
        result.stopping = result.stopping || (result.running && stopping_);
    }
    return result;
}
////////////////////////////
// Project固有Runtimeの起動
////////////////////////////
Result<void> StandaloneSession::Start(const ProjectLaunchOptions& options) {
    if(!project_) return std::unexpected(Error(ErrorCategory::Framework,205,"Standalone project is not configured."));
    auto state = State(); if(!state) return std::unexpected(state.error());
    if(state->running || state->building) return std::unexpected(Failed("This Editor already owns a running Runtime or Runtime build."));
    auto launched = ProjectBuild::Launch(*project_,engine_,ProjectHostKind::Runtime,options);
    if(!launched) return std::unexpected(launched.error());
    child_ = std::move(*launched); runtimeLog_ = options.log;
    stopping_ = closeSent_ = exitReported_ = false; error_.clear(); return {};
}
////////////////////////////
// Runtimeの個別ビルドを開始する
////////////////////////////
Result<void> StandaloneSession::RequestStart(const ProjectLaunchOptions& options) {
    if(!project_) return std::unexpected(Failed("Standalone project is not configured."));
    auto state = State(); if(!state) return std::unexpected(state.error());
    if(state->running || state->building) return std::unexpected(Failed("A Runtime or Runtime build is already active."));
    if(options.frames > 10000 || (options.checkOnly && options.frames) || (!options.log.empty() && !options.log.is_absolute()))
        return std::unexpected(Failed("Invalid Runtime launch options."));
    auto uuid = CreateUuid(); if(!uuid) return std::unexpected(uuid.error());
    auto logs = ResolveProjectPath(project_->Root(),"Build/Logs/Standalone");
    if(!logs) return std::unexpected(logs.error());
    std::error_code error; std::filesystem::create_directories(*logs,error);
    if(error) return std::unexpected(Failed("Cannot create Runtime log directory: " + error.message()));
    buildLog_ = *logs / (*uuid + ".build.log");
    pendingOptions_ = options;
    if(pendingOptions_.log.empty()) pendingOptions_.log = *logs / (*uuid + ".runtime.log");
    runtimeLog_ = pendingOptions_.log; error_.clear(); child_.reset();
    stopping_ = closeSent_ = exitReported_ = false;
    // データ専用Projectは既存の汎用ホストを利用し、存在しないC++ターゲットを要求しない。
    const bool hasTargets = std::filesystem::exists(project_->Config() / "BuildTargets.json",error);
    if(error) return std::unexpected(Failed("Cannot inspect Runtime build targets: " + error.message()));
    if(!hasTargets) return Start(pendingOptions_);
    cancelBuild_ = std::stop_source{};
    try {
        build_ = std::async(std::launch::async,[project = project_,engine = engine_,log = buildLog_,stop = cancelBuild_.get_token()]() -> Result<std::filesystem::path> {
            try { return ProjectBuild::Build(*project,engine,ProjectHostKind::Runtime,log,stop); }
            catch(const std::exception& exception) { return std::unexpected(Failed(exception.what())); }
        });
    } catch(const std::exception& exception) { return std::unexpected(Failed(exception.what())); }
    return {};
}
////////////////////////////
// ビルド完了と子Runtimeの通常終了を処理する
////////////////////////////
Result<void> StandaloneSession::Poll() {
    const auto fail = [this](Error error) -> Result<void> { error_ = error.GetMessageText(); return std::unexpected(std::move(error)); };
    if(build_.valid() && build_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto result = build_.get();
        // 取消と完了が同時でも、取消を受けた要求からRuntimeを起動しない。
        if(cancelBuild_.stop_requested()) return {};
        if(!result) return fail(result.error());
        auto started = Start(pendingOptions_); if(!started) return fail(started.error());
    }
    if(!child_) return {};
    auto state = child_->Poll(); if(!state) return fail(state.error());
    if(*state) {
        stopping_ = false;
        if(!exitReported_) {
            exitReported_ = true;
            if(**state != 0) return fail(Failed("Runtime exited with code " + std::to_string(**state) + ". Log: " + ConvertString(runtimeLog_.wstring())));
        }
        return {};
    }
    if(stopping_ && !closeSent_) {
        auto closed = child_->RequestClose();
        if(closed) closeSent_ = true;
        else if(std::chrono::steady_clock::now() >= closeDeadline_) {
            stopping_ = false; return fail(closed.error());
        }
    }
    return {};
}
////////////////////////////
// 所有するRuntimeへ通常終了を要求する
////////////////////////////
Result<void> StandaloneSession::Stop() {
    if(build_.valid()) { cancelBuild_.request_stop(); return {}; }
    if(!child_) return {};
    if(!stopping_) {
        stopping_ = true; closeSent_ = false;
        closeDeadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    }
    return Poll();
}
}
