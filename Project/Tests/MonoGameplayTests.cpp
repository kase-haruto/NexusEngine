#include "Runtime/Scripting/DotNetHost.h"
#include "Runtime/Scripting/NativeManagedBridge.h"
#include "Runtime/Scripting/CSharpRuntime.h"
#include "Runtime/Scripting/CSharpRuntimeSettings.h"
#include "Foundation/Logging/Logger.h"
#include "Runtime/Gameplay/GameApiBridge.h"
#include "Tests/Fixtures/SilentWave.h"
#include "Runtime/Level/Level.h"
#include "Runtime/Scene/Components/ScriptComponent.h"
#include "Runtime/Scene/Components/TransformComponent.h"
#include "Runtime/Scene/Components/FogComponent.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace NexusEngine;
namespace {
void Require(bool condition, const char* message) { if(!condition) throw std::runtime_error(message); }
void Check(const Result<void>& result) { if(!result) throw std::runtime_error(result.error().GetMessageText()); }
NexusGameStatus NEXUS_GAME_CALL Log(uint64_t, NexusGameString message) noexcept {
    std::cout.write(message.data, message.length); std::cout << '\n'; return NEXUS_GAME_OK;
}
/** \brief Process内のMono rootを再利用し、子Domainの生成・解放とthread拒否を確認する */
void Hosting(const CSharpRuntimeSettings& settings) {
    for(int attempt = 0; attempt < 2; ++attempt) {
        DotNetHost host; Check(host.Initialize(settings)); Require(host.IsMono(), "CoreCLR was selected instead of Mono.");
        bool rejected = false;
        std::thread other([&] { rejected = !host.Resolve(settings.bootstrapAssembly, L"Start"); }); other.join();
        Require(rejected, "Wrong-thread Mono entry lookup was accepted.");
        Require(!host.Resolve(settings.bootstrapAssembly.parent_path() / "Nexus.Managed.dll", L"Start"),
            "Mono resolved an entry from an assembly outside its Bootstrap contract.");
        NativeManagedBridge bridge;
        const NexusManagedBootstrapContextV1 context{1, sizeof(NexusManagedBootstrapContextV1), 42, Log};
        Check(bridge.Start(host, settings.bootstrapAssembly, context));
        Require(bridge.Stop(), "Mono bootstrap cleanup failed.");
        host.Shutdown(); Require(!host.IsQuarantined(), "Mono child Domain did not unload.");
    }
}
/** \brief Monoでも既存Level / ScriptSystemを使い、Domain差替えと公開値を保持する */
void Gameplay(CSharpRuntimeSettings settings, const std::filesystem::path& first, const std::filesystem::path& second) {
    Level level; Check(level.LoadEmpty());
    auto entity = level.CreateEntity("Mono Entity");
    const auto id = entity.GetComponent<PersistentIdComponent>()->value;
    auto* script = entity.AddComponent<ScriptComponent>(); script->scriptType = "mono.probe";
    script->propertyState = R"({"schemaVersion":1,"types":{"mono.probe":{"version":1,"values":{"speed":{"Float":3}}}}})";
    ScriptAttachment secondary; secondary.scriptType = "mono.secondary";
    secondary.propertyState = R"({"schemaVersion":1,"types":{"mono.secondary":{"version":1,"values":{"lift":{"Float":4}}}}})";
    script->additional.push_back(std::move(secondary));
    GameApiBridge world(level); CSharpRuntime runtime; settings.gameAssembly = first;
    const auto baseline = level.GetSystemScheduler().GetSystems().size();
    Check(runtime.Start(settings, world.World(), level));
    Check(level.Update(1));
    Require(level.FindEntity(id).GetComponent<TransformComponent>()->translation.x == 3, "Mono did not update the shared Transform.");
    Require(level.FindEntity(id).GetComponent<TransformComponent>()->translation.y == 4, "Second Script did not update the same Entity.");
    auto* attached = level.FindEntity(id).GetComponent<ScriptComponent>();
    Require(attached->additional[0].instance.value != 0 && attached->additional[0].instance.value != attached->instance.value, "Multiple Scripts share an instance ID.");
    const auto oldSecondary = attached->additional[0].instance.value;
    attached->additional[0].enabled = false;
    const auto* fog = level.FindEntity(id).GetComponent<FogComponent>();
    Require(fog && fog->startDistance == 3, "Mono Component API did not update the shared Fog.");
    const auto oldInstance = level.FindEntity(id).GetComponent<ScriptComponent>()->instance.value;
    Require(oldInstance != 0, "Mono Script was not attached.");
    Check(runtime.Reload(second, level, world));
    Require(runtime.ReloadState() == ManagedReloadState::Succeeded, "Mono Domain reload did not complete.");
    Require(level.FindEntity(id).GetComponent<ScriptComponent>()->instance.value > oldInstance, "Mono reload reused an old instance ID.");
    attached = level.FindEntity(id).GetComponent<ScriptComponent>();
    Require(attached->additional.size() == 1 && attached->additional[0].instance.value > oldSecondary && !attached->additional[0].enabled && attached->enabled, "Reload lost per-Script identity or enabled state.");
    Check(level.Update(1));
    Require(level.FindEntity(id).GetComponent<TransformComponent>()->translation.x == 9, "Mono reload did not apply new code and saved Speed.");
    Require(level.FindEntity(id).GetComponent<TransformComponent>()->translation.y == 4, "Disabled secondary Script updated after Reload.");
    Require(!runtime.Reload(second.parent_path() / "Missing.dll", level, world), "Missing Mono candidate was accepted.");
    Check(level.Update(1));
    Require(level.FindEntity(id).GetComponent<TransformComponent>()->translation.x == 15, "Rejected candidate stopped current Mono code.");
    runtime.Stop(); Require(!runtime.IsQuarantined(), "Mono shutdown entered quarantine.");
    Require(level.GetSystemScheduler().GetSystems().size() == baseline, "Mono left ScriptSystem attached after Stop.");
    Check(runtime.Start(settings, world.World(), level)); runtime.Stop();
    Require(!runtime.IsQuarantined(), "Mono session restart failed.");
}
/** \brief 同型2instanceの独立保存・Reloadと、新しいManaged engine APIを実行する */
void RepeatedGameplay(CSharpRuntimeSettings settings,const std::filesystem::path& first,const std::filesystem::path& second) {
    Level level; Check(level.LoadEmpty());
    const auto root=std::filesystem::absolute("Generated/MonoRepeatedGameplay");
    Require(NexusTests::WriteSilentWave(root/"silence.wav"),"Cannot create WAV fixture.");
    Check(level.SetResourceRoot(root));
    Check(level.Save(root/"Next.level"));
    InputFrame input; input.focused=true; input.Key('W',true); input.Key('L',true);
    input.gamepads[0].Update({true,0x1000,32767,0,0,0,0,255},true);
    level.SetFrameInput(input);
    auto entity=level.CreateEntity("Repeated Mono"); const auto id=entity.GetComponent<PersistentIdComponent>()->value;
    auto* script=entity.AddComponent<ScriptComponent>(); script->scriptType="mono.repeated";
    script->propertyState=R"({"schemaVersion":1,"types":{"mono.repeated":{"version":1,"values":{"step":{"Float":2}}}}})";
    ScriptAttachment extra; extra.scriptType="mono.repeated";
    extra.propertyState=R"({"schemaVersion":1,"types":{"mono.repeated":{"version":1,"values":{"step":{"Float":5}}}}})";
    script->additional.push_back(std::move(extra));
    const auto firstId=script->slotId,secondId=script->additional[0].slotId;
    GameApiBridge world(level); CSharpRuntime runtime; settings.gameAssembly=first;
    Check(runtime.Start(settings,world.World(),level)); Check(level.Update(1));
    auto requested=level.TakeLevelRequest(); Require(requested && requested->filename()=="Next.level","Managed Level request was not queued.");
    input.ClearTransitions(); level.SetFrameInput(input);
    Require(level.FindEntity(id).GetComponent<TransformComponent>()->translation.z==7,"Repeated Mono instances did not update independently.");
    auto* running=level.FindEntity(id).GetComponent<ScriptComponent>();
    Require(running->instance.value && running->additional[0].instance.value && running->instance.value!=running->additional[0].instance.value,"Repeated Mono instances share a runtime ID.");
    Check(runtime.Reload(second,level,world));
    running=level.FindEntity(id).GetComponent<ScriptComponent>();
    Require(running->slotId==firstId && running->additional[0].slotId==secondId,"Reload changed saved instance identity.");
    Check(level.Update(1));
    Require(level.FindEntity(id).GetComponent<TransformComponent>()->translation.z==21,"Reload mixed repeated public state or retained old code.");
    running=level.FindEntity(id).GetComponent<ScriptComponent>(); running->additional[0].enabled=false;
    Check(level.Update(1));
    Require(level.FindEntity(id).GetComponent<TransformComponent>()->translation.z==25,"Repeated instance Enabled leaked into another instance.");
    runtime.Stop(); Require(!runtime.IsQuarantined(),"Repeated Mono or Audio cleanup retained references.");
    std::cout << "PASS: repeated Mono Script state/identity/reload and actual Managed Input/Physics/Audio\n";
}
}
/** \brief 実Mono SDKと生成済みManaged fixtureを指定してCPU統合試験を行う */
int wmain(int argc, wchar_t** argv) {
    Logger::Get().InitializeDefaultSinks();
    try {
        Require(argc == 5, "Usage: NexusMonoGameplayTests <MonoRoot> <Bootstrap.dll> <v1.dll> <v2.dll>");
        CSharpRuntimeSettings settings;
        settings.monoRoot = std::filesystem::absolute(argv[1]); settings.bootstrapAssembly = std::filesystem::absolute(argv[2]);
        Hosting(settings);
        Gameplay(settings, std::filesystem::absolute(argv[3]), std::filesystem::absolute(argv[4]));
        RepeatedGameplay(settings,std::filesystem::absolute(argv[3]),std::filesystem::absolute(argv[4]));
        std::cout << "PASS: actual Mono hosting, Domain unload/restart, shared ECS, lifecycle, reload and state restoration.\n";
        return 0;
    } catch(const std::exception& exception) { std::cerr << "FAIL: " << exception.what() << '\n'; return 1; }
}
