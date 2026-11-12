#include "Runtime/Application/GameRuntime.h"
#include "Runtime/Gameplay/GameApiBridge.h"
#include "Runtime/Scripting/CSharpRuntime.h"
#include "Runtime/Scripting/ScriptSystem.h"
#include "Runtime/Project/ProjectJson.h"
#include "Runtime/Scene/Components/ScriptComponent.h"
#include "Editor/Core/EditorSession.h"
#include "Foundation/Logging/BufferedLogSink.h"
#include "Foundation/Logging/Logger.h"
#include "Platform/FileSystem/AtomicFile.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <fstream>
#include <thread>
#include <chrono>
#include <iterator>
#include <vector>

using namespace NexusEngine;
namespace {
void Require(bool condition, const char* message) { if(!condition) throw std::runtime_error(message); }
std::unique_ptr<Level> ScriptLevel(const char* type) {
    auto level = std::make_unique<Level>(); Require(level->LoadEmpty().has_value(), "Cannot create World.");
    auto entity = level->CreateEntity("Scripted");
    auto* script = entity.AddComponent<ScriptComponent>(); Require(script != nullptr, "Cannot attach ScriptComponent."); script->scriptType = type;
    return level;
}
Entity Find(Level& level, const char* name) {
    Entity result; level.GetScene()->ForEach<NameComponent>([&](Entity entity, const NameComponent& component) { if(component.name == name) result = entity; }); return result;
}
void Start(GameRuntime& runtime, const char* type) { auto result = runtime.Start(ScriptLevel(type)); if(!result) throw std::runtime_error(result.error().GetMessageText()); }
size_t Count(const BufferedLogBuffer& logs, const char* message) {
    const auto values = logs.Snapshot(); return static_cast<size_t>(std::ranges::count_if(values, [&](const auto& record) { return record.message == message; }));
}
/** \brief Hosting開始前のWorld対応検証。誤WorldではSDKやファイルの読込にも進ませない */
void ValidateStartupOwnership() {
    Level first, second; Require(first.LoadEmpty().has_value() && second.LoadEmpty().has_value(), "Cannot create validation Worlds.");
    GameApiBridge firstWorld(first), secondWorld(second); CSharpRuntime session;
    const auto mismatch = session.Start({}, firstWorld.World(), second);
    Require(!mismatch && mismatch.error().GetMessageText().find("matching World") != std::string::npos, "Mismatched World reached Hosting.");
    bool rejectedThread = false;
    std::thread worker([&] { const auto result = session.Start({}, firstWorld.World(), first); rejectedThread = !result && result.error().GetMessageText().find("matching World") != std::string::npos; });
    worker.join(); Require(rejectedThread && !session.IsQuarantined(), "Wrong-thread startup reached Hosting or changed the session.");
}
/** \brief 同じLevelで停止・再開始・初期化失敗してもSchedulerにScriptSystemを残さない */
void SessionRegistrationLifecycle(const CSharpRuntimeSettings& settings) {
    auto level = ScriptLevel("Fixture.Probe"); GameApiBridge world(*level); CSharpRuntime session;
    const auto baseline = level->GetSystemScheduler().GetSystems().size();
    for(int attempt = 0; attempt < 2; ++attempt) {
        auto result = session.Start(settings, world.World(), *level);
        if(!result) throw std::runtime_error("Cannot restart Managed session on the same Level: " + result.error().GetMessageText());
        Require(level->GetSystemScheduler().GetSystems().size() == baseline + 1, "ScriptSystem was missing or accumulated.");
        session.Stop(); session.Stop();
        Require(!session.IsQuarantined() && level->GetSystemScheduler().GetSystems().size() == baseline, "Stop retained a detached ScriptSystem.");
        Require(Find(*level, "Scripted").GetComponent<ScriptComponent>()->instance.value == 0, "Stop retained a runtime instance handle.");
    }
    Find(*level, "Scripted").GetComponent<ScriptComponent>()->scriptType = "Fixture.MissingType";
    Require(!session.Start(settings, world.World(), *level), "Unknown Script type was accepted.");
    Require(level->GetSystemScheduler().GetSystems().size() == baseline && !session.IsQuarantined(), "Partial initialization retained Scheduler registration or Managed references.");
}
/** コピー不可ComponentでReloadのClone失敗を起こす値。別Worldや別ECSは作らない。 */
struct ReloadCloneBarrier { std::unique_ptr<int> value = std::make_unique<int>(1); };
/** 任意C++ ComponentのCloneがstd::exception以外を送出しても境界で捕捉する検証用。 */
struct ThrowingReloadClone {
    ThrowingReloadClone() = default;
    ThrowingReloadClone(const ThrowingReloadClone&) { throw 17; }
    ThrowingReloadClone(ThrowingReloadClone&&) noexcept = default;
    ThrowingReloadClone& operator=(ThrowingReloadClone&&) noexcept = default;
};
/** \brief Commit前の取消で旧Behaviour / instanceを維持する。SDKの実行はユーザーによるテスト時だけ行う */
void PreparedReloadCancellation(const CSharpRuntimeSettings& settings, const BufferedLogBuffer& logs) {
    auto level = ScriptLevel("Fixture.Probe"); GameApiBridge world(*level); DotNetHost host; NativeManagedBridge bridge;
    Require(host.Initialize(settings).has_value(), "Cannot initialize cancellation fixture host.");
    const auto path = ProjectJson::Utf8(settings.gameAssembly);
    const NexusManagedGameplayContextV3 context{3, sizeof(NexusManagedGameplayContextV3), world.World(), GameApiBridge::Api(), GameApiBridge::Scripts(),
        {path.data(), static_cast<uint32_t>(path.size())}, GameApiBridge::Properties()};
    Require(bridge.StartReflectedGameplay(host, settings.bootstrapAssembly, context).has_value(), "Cannot start cancellation fixture.");
    ScriptSystem scripts(*level, bridge, world.World()); Require(scripts.Initialize().has_value(), "Cannot initialize cancellation fixture.");
    auto entity = Find(*level, "Scripted"); const auto instance = entity.GetComponent<ScriptComponent>()->instance.value;
    const auto creates = Count(logs, "Probe.Create"), destroys = Count(logs, "Probe.Destroy");
    Require(instance && scripts.RebindScene(false) && entity.GetComponent<ScriptComponent>()->instance.value == instance, "Prepare reconnect discarded an active instance.");
    Require(bridge.Reload(0, settings.gameAssembly).has_value() && bridge.Reload(2).has_value() && !bridge.IsSuspended(), "Prepare cancellation retired the running generation.");
    Require(scripts.Update(*level->GetScene(), .25f).has_value(), "Old generation did not run after cancellation.");
    Require(entity.GetComponent<ScriptComponent>()->instance.value == instance && Count(logs, "Probe.Create") == creates && Count(logs, "Probe.Destroy") == destroys, "Cancellation recreated an unchanged Behaviour.");
    scripts.Detach(); Require(bridge.Stop(), "Cannot stop cancellation fixture.");
}
/** \brief cleanup失敗と単なるUnload待ちを区別し、同sessionの再開拒否・残りの解除を検証するソース */
void CleanupFailureContracts(const CSharpRuntimeSettings& settings, BufferedLogBuffer& logs) {
    for(const auto* type : {"Fixture.CleanupResources", "Fixture.CleanupDestroy", "Fixture.CleanupCancellation"}) {
        logs.Clear();
        auto level = ScriptLevel(type); GameApiBridge world(*level); DotNetHost host; NativeManagedBridge bridge;
        Require(host.Initialize(settings).has_value(), "Cannot initialize cleanup fixture host.");
        const auto path = ProjectJson::Utf8(settings.gameAssembly);
        const NexusManagedGameplayContextV3 context{3, sizeof(NexusManagedGameplayContextV3), world.World(), GameApiBridge::Api(), GameApiBridge::Scripts(),
            {path.data(), static_cast<uint32_t>(path.size())}, GameApiBridge::Properties()};
        Require(bridge.StartReflectedGameplay(host, settings.bootstrapAssembly, context).has_value(), "Cannot start cleanup fixture.");
        ScriptSystem scripts(*level, bridge, world.World()); Require(scripts.Initialize().has_value(), "Cannot create cleanup fixture Behaviour.");
        Require(bridge.Reload(0, settings.gameAssembly).has_value(), "Cleanup fixture preflight unexpectedly failed.");
        Require(!bridge.Reload(1) && bridge.IsSuspended(), "Failed cleanup was reported as successful Reload.");
        Require(!bridge.Reload(0, settings.gameAssembly) && bridge.IsSuspended(), "GC completion cleared a cleanup failure.");
        Require(!bridge.Reload(2) && bridge.IsSuspended(), "Abort converted failed cleanup to successful shutdown.");
        scripts.Detach(); Require(!bridge.Stop(), "Stop hid a prior cleanup failure.");
        if(std::string_view(type) == "Fixture.CleanupResources") {
            Require(Count(logs, "Cleanup.ResourceFailure") == 1 && Count(logs, "Cleanup.ResourceRemaining") == 1 && Count(logs, "Cleanup.ResourcesDestroy") == 1,
                "A failing Dispose skipped remaining resources or repeated lifecycle callbacks.");
        }
        if(std::string_view(type) == "Fixture.CleanupCancellation")
            Require(Count(logs, "Cleanup.CancellationDestroy") == 1, "Cancellation failure skipped OnDestroy.");
    }
    logs.Clear();
    auto level = ScriptLevel("Fixture.CleanupDisable"); GameApiBridge world(*level); DotNetHost host; NativeManagedBridge bridge;
    Require(host.Initialize(settings).has_value(), "Cannot initialize disable fixture host.");
    const auto path = ProjectJson::Utf8(settings.gameAssembly);
    const NexusManagedGameplayContextV3 context{3, sizeof(NexusManagedGameplayContextV3), world.World(), GameApiBridge::Api(), GameApiBridge::Scripts(),
        {path.data(), static_cast<uint32_t>(path.size())}, GameApiBridge::Properties()};
    Require(bridge.StartReflectedGameplay(host, settings.bootstrapAssembly, context).has_value(), "Cannot start disable fixture.");
    ScriptSystem scripts(*level, bridge, world.World()); Require(scripts.Initialize().has_value(), "Cannot create disable Behaviour.");
    Find(*level, "Scripted").GetComponent<ScriptComponent>()->enabled = false;
    Require(!scripts.Update(*level->GetScene(), .1f), "OnDisable cleanup failure was reported as a successful Tick.");
    scripts.Detach(); Require(!bridge.Stop(), "Stop hid an OnDisable cleanup failure.");
    Require(Count(logs, "Cleanup.Disable") == 1 && Count(logs, "Cleanup.DisableDestroy") == 1, "Disable failure prevented or repeated final cleanup.");
}
/** \brief 実Assemblyのidentity・非参照copy・大文字小文字・開始失敗後の復旧を既存Host経由で検証するソース */
void ManagedAssemblyBoundaries(const CSharpRuntimeSettings& settings, const std::filesystem::path& reload, BufferedLogBuffer& logs) {
    logs.Clear();
    {
        GameRuntime runtime; Require(runtime.SetCSharpRuntime(settings), "Cannot configure Assembly boundary probe.");
        Start(runtime, "Fixture.AssemblyBoundaryProbe");
        Require(Count(logs, "Assembly.BootstrapBlocked") == 1, "Case-insensitive Assembly reference bypassed the Bootstrap boundary."); runtime.Stop();
    }
    auto id = CreateUuid(); Require(id.has_value(), "Cannot create dependency fixture ID.");
    const auto root = std::filesystem::absolute("Generated/ManagedDependencyValidation") / *id, source = root / "Managed";
    std::filesystem::create_directories(root);
    // 実GameScriptsはロードせずcopyする。利用者のBuild出力・既存Projectを上書きしない。
    std::filesystem::copy(settings.gameAssembly.parent_path(), source, std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_symlinks);
    const auto alias = source / "Alias.dll", roster = source / "GameScripts.modules.json";
    std::filesystem::copy_file(settings.gameAssembly, alias);
    const auto writeRoster = [&](const ProjectJson::Json& roots) { Require(ProjectJson::Write(roster, roots).has_value(), "Cannot write dependency fixture roster."); };
    auto copied = settings; copied.gameAssembly = source / settings.gameAssembly.filename();
    writeRoster(ProjectJson::Json::array({"GameScripts.dll"}));
    logs.Clear(); GameRuntime runtime; Require(runtime.SetCSharpRuntime(copied), "Cannot configure copied Assembly fixture.");
    Start(runtime, "Fixture.Probe"); runtime.Stop();
    Require(Count(logs, "Probe.Create") == 1, "An unreferenced Assembly copy changed the active root set.");
    const auto shadow = std::filesystem::temp_directory_path() / "NexusEngine.Managed" / std::to_string(GetCurrentProcessId());
    const auto shadowDirectories = [&] {
        std::vector<std::filesystem::path> result;
        if(std::filesystem::exists(shadow)) for(const auto& entry : std::filesystem::directory_iterator(shadow)) if(entry.is_directory()) result.push_back(entry.path());
        std::ranges::sort(result); return result;
    };
    const auto previous = shadowDirectories();
    // filenameが異なっても同じAssembly identityのrootを二重に宣言してはならない。
    writeRoster(ProjectJson::Json::array({"GameScripts.dll", "Alias.dll"}));
    Require(!runtime.Start(ScriptLevel("Fixture.Probe")) && !runtime.IsRunning(), "Duplicate root Assembly identity was accepted.");
    std::filesystem::copy_file(reload, alias, std::filesystem::copy_options::overwrite_existing);
    Require(!runtime.Start(ScriptLevel("Fixture.Probe")) && !runtime.IsRunning(), "Conflicting root Assembly versions were accepted.");
    Require(Count(logs, "Probe.Create") == 1 && Count(logs, "Managed bootstrap: FileLoadException: Duplicate or reserved Managed root Assembly identity: GameScripts") == 2,
        "Root rejection executed gameplay or failed for an unrelated reason.");
    Require(shadowDirectories() == previous, "Constructor failure retained an unowned shadow generation.");
    // 非参照ファイルは有効なModuleではない。異なるRID / 未使用copyまで一律拒否しない。
    writeRoster(ProjectJson::Json::array({"GameScripts.dll"}));
    Start(runtime, "Fixture.Probe"); runtime.Stop();
    Require(Count(logs, "Probe.Create") == 2, "Valid roots could not restart after duplicate rejection.");
}
/* callback中の停止要求でWorldが直ちに破棄されず、Transform APIを最後まで使えることを検証する。 */
class StopSink final : public ILogSink {
public:
    GameRuntime* runtime = nullptr; //< テスト中だけ借用し、Stopの完了後に解除する
    void Write(const LogRecord& record) noexcept override { if(runtime && record.message == "RequestStop") runtime->Stop(); }
};
}

/** \brief 共通World・実Hosting・Gameplay・Reloadを実行する統合回帰テスト */
int main() {
    // Interopの戻り値だけではManaged例外の原因が失われるため、失敗時も直前の共通Logger記録を表示する。
    auto logs = std::make_shared<BufferedLogBuffer>();
    try {
        ValidateStartupOwnership();
        std::wstring executable(32768, L'\0'); const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        Require(length && length < executable.size(), "Cannot locate host."); executable.resize(length);
        const auto root = std::filesystem::path(executable).parent_path();
        const CSharpRuntimeSettings settings{root / "nethost.dll", root / "Managed/Nexus.Managed.Bootstrap.runtimeconfig.json", root / "Managed/Nexus.Managed.Bootstrap.dll", {}, root / "Managed/GameScripts.dll"};
        auto logSink = std::make_unique<BufferedLogSink>(logs); const auto* logObserver = logSink.get();
        auto stopper = std::make_unique<StopSink>(); auto* stopObserver = stopper.get();
        Require(Logger::Get().AddSink(std::move(logSink)) && Logger::Get().AddSink(std::move(stopper)), "Cannot observe script lifecycle.");
        struct RemoveSinks { const ILogSink* first; const ILogSink* second; ~RemoveSinks() { static_cast<void>(Logger::Get().RemoveSink(second)); static_cast<void>(Logger::Get().RemoveSink(first)); } } remove{logObserver, stopObserver};
        SessionRegistrationLifecycle(settings); logs->Clear(); PreparedReloadCancellation(settings, *logs); logs->Clear();
        CleanupFailureContracts(settings, *logs); logs->Clear();
        ManagedAssemblyBoundaries(settings, root / "ReloadV2/GameScripts.dll", *logs); logs->Clear();
        {
            EditorSession editor; Require(editor.Create().has_value(), "Cannot create future-version edit World.");
            auto entity = editor.GetLevel()->CreateEntity("Future Script"); auto* script = entity.AddComponent<ScriptComponent>(); script->scriptType = "fixture.reflected";
            script->propertyState = R"({"schemaVersion":1,"types":{"fixture.reflected":{"version":3,"values":{"speed":{"Float":99}}}}})";
            const auto before = script->propertyState; auto* editing = editor.GetLevel()->GetScene();
            Require(editor.SetCSharpRuntime(settings) && !editor.StartPlay(), "Old script code accepted a newer saved contract.");
            Require(editor.GetLevel()->GetScene() == editing && entity.IsAlive() && script->propertyState == before, "Rejected Play changed the edit Document.");
            logs->Clear();
        }
        {
            GameRuntime runtime; Require(runtime.SetCSharpRuntime(settings), "Cannot configure Component probe."); Start(runtime, "Fixture.ComponentProbe");
            auto entity = Find(*runtime.GetLevel(), "Scripted");
            Require(Count(*logs, "Component.Copy") == 1 && runtime.Update(.5f).has_value() && entity.GetComponent<TransformComponent>()->translation.x == .5f, "Transform retrieval did not return a detached value copy.");
            Require(entity.RemoveComponent<TransformComponent>() && runtime.Update(.5f).has_value(), "Missing Component was not handled safely.");
            Require(entity.IsAlive() && Count(*logs, "Component.Missing") == 1, "Component retrieval confused a missing Component with a new World.");
            runtime.Stop(); logs->Clear();
        }
        {
            GameRuntime runtime; Require(runtime.SetCSharpRuntime(settings), "Cannot configure Gameplay.");
            Start(runtime, "Fixture.Probe");
            auto entity = Find(*runtime.GetLevel(), "Scripted");
            Require(Count(*logs, "Probe.Create") == 1 && Count(*logs, "Probe.Enable") == 1 && Count(*logs, "Probe.Update") == 0, "Initial lifecycle ran OnUpdate or missed create/enable.");
            Require(runtime.Update(.5f).has_value(), "Tick failed.");
            Require(entity.GetComponent<TransformComponent>()->worldMatrix.At(3, 0) == .5f, "Managed Transform did not precede native Matrix evaluation.");
            runtime.TogglePause(); Require(runtime.Update(1).has_value(), "Pause failed.");
            Require(Count(*logs, "Probe.Update") == 1, "Paused script was updated."); runtime.TogglePause();
            entity.GetComponent<ScriptComponent>()->enabled = false; Require(runtime.Update(.25f).has_value(), "Disable failed.");
            Require(Count(*logs, "Probe.Disable") == 1 && Count(*logs, "Probe.Update") == 1, "Disabled behaviour was updated.");
            entity.GetComponent<ScriptComponent>()->enabled = true; Require(runtime.Update(.25f).has_value(), "Enable failed.");
            const auto firstInstance = entity.GetComponent<ScriptComponent>()->instance.value;
            Require(firstInstance != 0 && entity.RemoveComponent<ScriptComponent>(), "Cannot remove script.");
            auto* replacement = entity.AddComponent<ScriptComponent>(); replacement->scriptType = "Fixture.Probe";
            Require(runtime.Update(.25f).has_value(), "Reattach failed.");
            Require(replacement->instance.value != firstInstance && Count(*logs, "Probe.Destroy") == 1, "Old instance survived Component replacement.");
            runtime.Stop(); runtime.Stop(); Require(Count(*logs, "Probe.Destroy") == 2, "Stop did not destroy each instance once.");
            logs->Clear(); Start(runtime, "Fixture.SelfDestroy"); Require(runtime.Update(.1f).has_value(), "Self-destruction failed.");
            Require(runtime.GetLevel()->GetScene()->GetEntityCount() == 0 && Count(*logs, "Self.Disable") == 1 && Count(*logs, "Self.Destroy") == 1, "Self-destroy lifecycle or native ownership is incorrect."); runtime.Stop();
            logs->Clear(); Start(runtime, "Fixture.Spawn"); Require(runtime.Update(.1f).has_value(), "Spawn failed.");
            Require(Count(*logs, "Spawn.Destroy") == 1 && Count(*logs, "Probe.Create") == 0, "Spawned script was executed inside a stale snapshot.");
            Require(runtime.Update(.1f).has_value() && Count(*logs, "Probe.Create") == 1, "Spawned script was not synchronized next frame."); runtime.Stop();
            logs->Clear(); Start(runtime, "Fixture.Fault"); Require(runtime.Update(.1f).has_value() && runtime.Update(.1f).has_value(), "User exception destroyed World.");
            Require(runtime.IsRunning() && Count(*logs, "Fault.Disable") == 1, "Faulted behaviour was repeatedly enabled."); runtime.Stop();
            Start(runtime, "Fixture.RequestStop"); stopObserver->runtime = &runtime;
            Require(runtime.Update(.1f).has_value() && !runtime.IsRunning(), "Callback stop request was not deferred to the frame boundary."); stopObserver->runtime = nullptr;
        }
        {
            GameRuntime runtime; Require(runtime.SetCSharpRuntime(settings), "Cannot configure reflected Gameplay.");
            auto level = ScriptLevel("fixture.old");
            auto* script = Find(*level, "Scripted").GetComponent<ScriptComponent>();
            script->propertyState = R"({"schemaVersion":1,"types":{"fixture.old":{"version":1,"values":{"oldSpeed":{"Float":9},"removed":{"String":"retain"}}}}})";
            Require(runtime.Start(std::move(level)).has_value(), "Cannot restore reflected state.");
            auto entity = Find(*runtime.GetLevel(), "Scripted");
            Require(entity.GetComponent<TransformComponent>()->translation.x == 9, "Properties were not restored before OnCreate.");
            Require(runtime.Update(.5f).has_value(), "Reflected Tick failed.");
            const auto state = entity.GetComponent<ScriptComponent>()->propertyState;
            Require(state.find("9.5") != state.npos && state.find("retain") != state.npos, "Snapshot lost updated or unknown properties.");
            runtime.Stop();
            level = ScriptLevel("fixture.reflected"); Find(*level, "Scripted").GetComponent<ScriptComponent>()->propertyState = state;
            Require(runtime.Start(std::move(level)).has_value(), "Cannot recreate reflected script.");
            Require(Find(*runtime.GetLevel(), "Scripted").GetComponent<TransformComponent>()->translation.x == 9.5f, "Recreation lost the frame snapshot.");
            runtime.Stop();
        }
        {
            GameRuntime runtime; Require(runtime.SetCSharpRuntime(settings), "Cannot configure Reload test.");
            Start(runtime, "fixture.hot");
            auto entity = Find(*runtime.GetLevel(), "Scripted");
            const auto persistent = entity.GetComponent<PersistentIdComponent>()->value;
            const auto oldInstance = entity.GetComponent<ScriptComponent>()->instance.value;
            Require(runtime.Update(1).has_value() && entity.GetComponent<TransformComponent>()->translation.x == 3, "Initial generation failed.");
            auto* oldScene = runtime.GetLevel()->GetScene();
            Require(entity.AddComponent<ReloadCloneBarrier>() != nullptr, "Cannot attach clone barrier.");
            Require(!runtime.ReloadCSharp(root / "ReloadV2/GameScripts.dll") && runtime.GetLevel()->GetScene() == oldScene && !runtime.IsCSharpSuspended(), "Clone failure stopped the old Managed generation.");
            Require(entity.GetComponent<ScriptComponent>()->instance.value == oldInstance && entity.RemoveComponent<ReloadCloneBarrier>(), "Clone failure discarded the existing instance.");
            Require(entity.AddComponent<ThrowingReloadClone>() != nullptr, "Cannot attach throwing clone fixture.");
            const auto cloneException = runtime.ReloadCSharp(root / "ReloadV2/GameScripts.dll");
            Require(!cloneException && !runtime.IsCSharpSuspended() && runtime.GetLevel()->GetScene() == oldScene, "Unknown Clone exception escaped or retired the old generation.");
            Require(entity.RemoveComponent<ThrowingReloadClone>(), "Cannot remove throwing clone fixture.");
            const auto bad = root / "InvalidReload/GameScripts.dll"; std::filesystem::create_directories(bad.parent_path());
            { std::ofstream file(bad); file << "not an assembly"; }
            Require(!runtime.ReloadCSharp(bad) && runtime.GetLevel()->GetScene() == oldScene && !runtime.IsCSharpSuspended(), "Preflight failure changed the running generation.");
            Require(runtime.CSharpReloadState() == ManagedReloadState::CompatibilityError, "Preflight error was not classified separately.");
            Require(runtime.Update(1).has_value() && entity.GetComponent<TransformComponent>()->translation.x == 6, "Old assembly did not continue after preflight failure.");
            const auto v2 = root / "ReloadV2/GameScripts.dll";
            Require(runtime.ReloadCSharp(v2).has_value(), "Collectible reload did not succeed.");
            Require(runtime.CSharpReloadState() == ManagedReloadState::Succeeded, "Successful backend result was not reported.");
            entity = runtime.GetLevel()->FindEntity(persistent);
            Require(entity.IsAlive() && entity.GetComponent<ScriptComponent>()->instance.value > oldInstance && entity.GetComponent<TransformComponent>()->translation.x == 6, "Reload replaced Entity or lost state.");
            Require(runtime.Update(1).has_value() && entity.GetComponent<TransformComponent>()->translation.x == 12 && Count(*logs, "Hot.V2") == 1, "New generation did not execute.");
            Require(runtime.ReloadCSharp(settings.gameAssembly).has_value() && runtime.ReloadCSharp(v2).has_value(), "Repeated reload retained an old context.");
            const auto beforeFailure = runtime.GetLevel()->FindEntity(persistent).GetComponent<TransformComponent>()->translation.x;
            Require(!runtime.ReloadCSharp(root / "ReloadFailure/GameScripts.dll"), "Faulty OnCreate was accepted.");
            Require(runtime.IsRunning() && runtime.IsPaused() && runtime.IsCSharpSuspended(), "Initialization failure destroyed the World.");
            Require(runtime.CSharpReloadState() == ManagedReloadState::StateRestoreError, "Post-retirement error was reported as a compile or preflight error.");
            entity = runtime.GetLevel()->FindEntity(persistent);
            Require(entity.IsAlive() && entity.GetComponent<TransformComponent>()->translation.x == beforeFailure, "Failed generation mutations escaped the transaction.");
            Require(runtime.Update(1).has_value() && entity.IsAlive(), "Suspended scripts destroyed the retained World.");
            Require(runtime.ReloadCSharp(v2).has_value() && !runtime.IsCSharpSuspended() && !runtime.IsPaused() && runtime.CSharpReloadState() == ManagedReloadState::Succeeded, "Cannot recover and resume a suspended session.");
            runtime.TogglePause(); Require(runtime.IsPaused(), "Cannot pause recovered session.");
            Require(runtime.ReloadCSharp(settings.gameAssembly).has_value() && runtime.IsPaused(), "Successful Reload cleared the user's explicit Pause."); runtime.Stop();
        }
        {
            const auto release = root / "leak-release.flag";
            // テスト自身の新しいflag名を使い、既存ファイルを削除しない。
            const auto flag = release.wstring() + std::to_wstring(GetTickCount64());
            Require(SetEnvironmentVariableW(L"NEXUS_TEST_LEAK_RELEASE_FILE", flag.c_str()) != 0, "Cannot configure leak fixture.");
            GameRuntime runtime; Require(runtime.SetCSharpRuntime(settings), "Cannot configure leak test."); Start(runtime, "Fixture.Leaky");
            Require(!runtime.ReloadCSharp(root / "ReloadV2/GameScripts.dll") && runtime.IsRunning() && runtime.IsCSharpSuspended(), "Unload was reported successful while a worker / GCHandle retained the context.");
            { std::ofstream file{std::filesystem::path(flag)}; file << "release"; }
            bool recovered = false;
            for(int attempt = 0; attempt < 100 && !recovered; ++attempt) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                recovered = runtime.ReloadCSharp(root / "ReloadV2/GameScripts.dll").has_value();
            }
            Require(recovered, "Weak context remained blocked after releasing worker / GCHandle."); runtime.Stop();
            static_cast<void>(SetEnvironmentVariableW(L"NEXUS_TEST_LEAK_RELEASE_FILE", nullptr));
        }
        {
            EditorSession editor; Require(editor.Create().has_value(), "Cannot create editor World.");
            auto entity = editor.GetLevel()->CreateEntity("Editing"); auto* script = entity.AddComponent<ScriptComponent>(); script->scriptType = "Fixture.Probe";
            Require(editor.SetCSharpRuntime(settings) && editor.StartPlay() && editor.UpdatePreview(.5f).has_value(), "Editor Play failed.");
            Require(entity.GetComponent<TransformComponent>()->translation.x == 0 && script->instance.value == 0, "Managed Play changed the edit World."); editor.StopPlay();
            script->instance.value = 999; script->attachment = 777;
            const auto file = std::filesystem::absolute("Generated/ManagedGameplayTests/Roundtrip.level"); std::filesystem::create_directories(file.parent_path());
            Require(editor.GetLevel()->Save(file).has_value(), "Common Level save failed.");
            Level loaded; Require(loaded.Load(file).has_value(), "Common Level load failed."); auto* restored = Find(loaded, "Editing").GetComponent<ScriptComponent>();
            Require(restored && restored->scriptType == "Fixture.Probe" && restored->instance.value == 0 && restored->attachment == 0, "Runtime handles were serialized.");
        }
#ifdef NEXUS_HYBRID_FIXTURE
        {
            NativeGameModuleSettings native{std::filesystem::absolute("Generated/Outputs/EditorTests/NativeModuleFixture.dll"), "test-engine", "1.0.0", "11111111-1111-4111-8111-111111111111", "Fixture"};
            const auto patchSource = std::filesystem::absolute(std::filesystem::path("Generated/ManagedPatchProtocol") / std::to_wstring(GetTickCount64()));
            std::filesystem::create_directories(patchSource); { std::ofstream file(patchSource / "FixedState.h"); file << "// fixed protocol fixture\n"; }
            native.patchSourceDirectory = patchSource;
            GameRuntime runtime; Require(runtime.SetNativeGameModule(native) && runtime.SetCSharpRuntime(settings), "Cannot configure typed Native System probe.");
            Start(runtime, "fixture.bound"); Require(runtime.Update(.25f).has_value(), "Typed Native System Tick failed.");
            auto entity = Find(*runtime.GetLevel(), "Scripted");
            Require(entity.GetComponent<TransformComponent>()->translation.z == 7 && entity.GetComponent<TransformComponent>()->worldMatrix.At(3, 2) == 7 && Count(*logs, "Bound.Invoked") == 1, "Managed Facade did not reach the shared Native System / ECS.");
            Require(runtime.PrepareNativePatch().has_value() && runtime.Update(1).has_value() && Count(*logs, "Bound.Invoked") == 1, "Prepared Native boundary did not suspend Managed updates.");
            Require(!runtime.ReloadCSharp(root / "ReloadV2/GameScripts.dll"), "Managed Reload overlapped a Native patch.");
            // SDKパッチを実施せず、適用通知だけを模擬する。キャッシュしたHandleはcallback前に再解決する。
            Require(runtime.FinishNativePatch(true).has_value() && runtime.Update(.25f).has_value() && Count(*logs, "Bound.Invoked") == 2, "Cached Managed Binding did not recover exactly once after Native rebind.");
            runtime.Stop(); Require(GetModuleHandleW(native.file.c_str()) == nullptr, "Typed Binding retained its Native DLL.");
        }
        {
            const NativeGameModuleSettings native{std::filesystem::absolute("Generated/Outputs/EditorTests/NativeModuleFixture.dll"), "test-engine", "1.0.0", "11111111-1111-4111-8111-111111111111", "Fixture"};
            GameRuntime runtime; Require(runtime.SetNativeGameModule(native) && runtime.SetCSharpRuntime(settings), "Hybrid configuration was rejected.");
            Start(runtime, "Fixture.Hybrid");
            Require(runtime.GetLevel()->GetScene()->GetEntityCount() == 2, "Backends did not share one World.");
            Require(runtime.Update(.25f).has_value(), "Hybrid Tick failed.");
            auto entity = Find(*runtime.GetLevel(), "Scripted"); const auto id = entity.GetComponent<PersistentIdComponent>()->value;
            Require(entity.GetComponent<TransformComponent>()->translation.x == 1 && entity.GetComponent<TransformComponent>()->translation.y == 1, "Native public API and Managed code did not modify the same Entity.");
            Require(runtime.ReloadCSharp(root / "ReloadV2/GameScripts.dll").has_value(), "Hybrid Managed Reload failed.");
            Require(!runtime.ReloadCSharp(root / "ReloadFailure/GameScripts.dll") && runtime.IsCSharpSuspended(), "Reload allowed a Native state mutation in new OnCreate.");
            Require(runtime.GetLevel()->FindEntity(id).GetComponent<TransformComponent>()->translation.y == 1, "Failed Reload mutated the original World.");
            Require(runtime.ReloadCSharp(root / "ReloadV2/GameScripts.dll").has_value() && !runtime.IsPaused(), "Hybrid recovery did not resume after suspension.");
            Require(runtime.Update(.25f).has_value(), "Hybrid update after Reload failed."); entity = runtime.GetLevel()->FindEntity(id);
            Require(entity.GetComponent<TransformComponent>()->translation.x == 2 && entity.GetComponent<TransformComponent>()->translation.y == 2, "Native state was reset or mutated by failed Managed Reload.");
            runtime.Stop(); Require(GetModuleHandleW(native.file.c_str()) == nullptr, "Native DLL remained referenced after Hybrid Stop.");
        }
#endif
        {
        // Metadata exporterでは無効のconstructor負例を、この実行scopeだけで有効にする。
        // 元のprocess環境を保持し、例外による途中終了でも利用者の設定を復元する。
        constexpr wchar_t cleanupFlag[] = L"NEXUS_TEST_CLEANUP_CONSTRUCTORS";
        wchar_t previous[32768]{}; SetLastError(ERROR_SUCCESS);
        const auto count = GetEnvironmentVariableW(cleanupFlag, previous, static_cast<DWORD>(std::size(previous)));
        const auto error = GetLastError();
        Require(count < std::size(previous) && (count || error == ERROR_SUCCESS || error == ERROR_ENVVAR_NOT_FOUND), "Cannot capture cleanup fixture environment.");
        /* process環境の借用状態を復元し、負例flagを以降のテストや子processへ残さない。 */
        struct RestoreEnvironment {
            const wchar_t* name; //< このscopeより長寿命の名前だけを借用する
            std::wstring previous; bool present; //< 元の値と未設定を区別して復元する
            ~RestoreEnvironment() { static_cast<void>(SetEnvironmentVariableW(name, present ? previous.c_str() : nullptr)); }
        } restore{cleanupFlag, previous, count || error == ERROR_SUCCESS};
        Require(SetEnvironmentVariableW(cleanupFlag, L"1") != 0, "Cannot enable cleanup constructor fixture.");
        for(const auto* type : {"Fixture.CleanupPreflight", "Fixture.CleanupUnload"})
        {
            // cleanup契約違反によるProcessのpatch禁止は意図的に残るため、他のpatch検証より後に行う。
            GameRuntime runtime; Require(runtime.SetCSharpRuntime(settings), "Cannot configure preflight cleanup fixture.");
            Start(runtime, type); auto* original = runtime.GetLevel()->GetScene();
            Require(!runtime.ReloadCSharp(settings.gameAssembly) && runtime.IsCSharpSuspended() && runtime.CSharpReloadState() == ManagedReloadState::StateRestoreError,
                "Preflight cleanup failure was reported as a recoverable compatibility error.");
            Require(runtime.GetLevel()->GetScene() == original, "Failed preflight exchanged the native World.");
            Require(!runtime.ReloadCSharp(settings.gameAssembly) && runtime.IsCSharpSuspended(), "Failed candidate cleanup allowed automatic recovery.");
            runtime.Stop();
        }
        }
        std::cout << "PASS: common ECS, lifecycle, Transform order, pause, detach/reattach, self-destroy, spawn, fault isolation, deferred Stop, edit/Play isolation and common serialization.\n"; return 0;
    } catch(const std::exception& exception) {
        std::cerr << "FAIL: " << exception.what() << '\n';
        const auto records = logs->Snapshot(); const auto begin = records.size() > 40 ? records.size() - 40 : 0;
        for(size_t index = begin; index < records.size(); ++index) std::cerr << records[index].message << '\n';
        return 1;
    }
}
