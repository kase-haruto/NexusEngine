#include "Runtime/Application/GameRuntime.h"
#include "Runtime/Project/ProjectService.h"
#include "Runtime/Scene/Components/NameComponent.h"
#include "Runtime/Scene/Components/TransformComponent.h"
#include "Runtime/Scene/Components/ScriptComponent.h"
#include "Platform/Development/LiveCodingAgent.h"
#include "Foundation/Logging/Logger.h"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace NexusEngine;
namespace {
/** \brief 実SDKの診断を残し、失敗をProcessの終了値へ伝える */
void Require(bool condition, const char* message) { if(!condition) throw std::runtime_error(message); }
/** \brief 同じWorldのNativeGameを名前で解決する。Module内部stateへのpointerは取得しない */
Entity NativeEntity(Level& level) {
    Entity result;
    level.GetScene()->ForEach<NameComponent>([&](Entity entity, const NameComponent& name) { if(name.name == "NativeGame") result = entity; });
    return result;
}
}

/**
 * \brief NativeModuleテンプレートのbody-onlyパッチを実SDKで確認するCPU専用ホスト
 * \param argv Project、対応NativeLivePatch DLL、新規signal directory、任意のManaged配置Rootの絶対配置
 * \return 同一World / Entity保持とtarget=5から9への変更を確認した場合だけ0
 * \note Ready後に生成ProjectのMain.cppのTargetRequestの5を9へ変更し、Requestファイルを作成する
 */
int wmain(int argc, wchar_t** argv) {
    Logger::Get().InitializeDefaultSinks();
    try {
        Require(argc == 4 || argc == 5, "Usage: NativeModuleLivePatchSmoke <project.nxproject> <Game.dll> <new-signal-directory> [managed-host-output]");
        const auto opened = ProjectService::Open(std::filesystem::absolute(argv[1]));
        if(!opened) throw std::runtime_error(opened.error().GetMessageText());
        const auto& project = opened->Descriptor();
        const ProjectModuleDescriptor* native = nullptr;
        for(const auto& module : project.modules) if(module.language == ProjectModuleLanguage::Cpp) { Require(!native, "Requires exactly one Native Module."); native = &module; }
        Require(native != nullptr, "Requires a NativeModule template Module.");
        const auto signals = std::filesystem::absolute(argv[3]);
        Require(!std::filesystem::exists(signals), "Use a new signal directory; existing files are never removed.");
        std::filesystem::create_directories(signals);
        NativeGameModuleSettings settings;
        settings.file = std::filesystem::absolute(argv[2]); settings.engineId = project.engine.id; settings.engineVersion = project.engine.version;
        settings.projectId = project.projectId; settings.moduleId = native->id;
        settings.patchSourceDirectory = opened->Root() / native->path; settings.patchProjectDirectory = opened->Root(); settings.livePatch = true;
        settings.registerPatchRuntime = LiveCodingAgent::RegisterGameRuntime; settings.unregisterPatchRuntime = LiveCodingAgent::UnregisterGameRuntime;
        // RuntimeをAgentより先に構築し、SDKのObserverをDLL / Worldより先に解除する。
        GameRuntime runtime; Require(runtime.SetNativeGameModule(settings), "Cannot configure Native Module.");
        auto level = std::make_unique<Level>(); Require(level->LoadEmpty().has_value(), "Cannot create World.");
        Entity managedEntity;
        if(argc == 5) {
            const auto root = std::filesystem::absolute(argv[4]);
            Require(runtime.SetCSharpRuntime({root / "nethost.dll", root / "Managed/Nexus.Managed.Bootstrap.runtimeconfig.json", root / "Managed/Nexus.Managed.Bootstrap.dll", {}, root / "Managed/GameScripts.dll"}), "Cannot configure Managed patch probe.");
            managedEntity = level->CreateEntity("ManagedPatchProbe");
            managedEntity.AddComponent<ScriptComponent>()->scriptType = "smoke.nativepatch";
        }
        auto started = runtime.Start(std::move(level)); if(!started) throw std::runtime_error(started.error().GetMessageText());
        LiveCodingAgent agent(true); Require(agent.IsGameModuleBackend(), "Build the test with NativeLivePatch enabled.");
        Require(runtime.Update(10).has_value(), "Initial Native update failed.");
        auto* originalWorld = runtime.GetLevel(); const auto entity = NativeEntity(*originalWorld);
        Require(entity.IsAlive() && std::abs(entity.GetComponent<TransformComponent>()->translation.x - 5) < .001f, "Initial target must be 5.");
        const auto entityCount = originalWorld->GetScene()->GetEntityCount();
        std::ofstream(signals / "Ready") << "target=5\n";
        std::cout << "READY: change only generated Main.cpp TargetRequest from 5 to 9, then create Request.\n" << std::flush;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(2);
        bool requested = false, checkingCompileError = false, compileErrorPreserved = false;
        while(std::chrono::steady_clock::now() < deadline) {
            if(!requested && !compileErrorPreserved && std::filesystem::exists(signals / "RequestError")) {
                Require(LiveCodingAgent::RequestReload(), "Native compile-error request was rejected.");
                requested = true; checkingCompileError = true;
            } else if(!requested && std::filesystem::exists(signals / "Request")) {
                Require(LiveCodingAgent::RequestReload(), "Native Reload request was rejected."); requested = true;
            }
            // Engineと同じ所有Threadの安全な境界で、同期SDKのCompileと適用を進める。
            if(agent.WantsReload()) agent.ReloadGameModule();
            const auto state = LiveCodingAgent::State();
            if(requested && checkingCompileError && state == NativeReloadState::CompileError) {
                // Compile失敗ではDLLもBindingも交換せず、同じWorldの旧ゲーム処理を続行できる。
                Require(runtime.GetLevel() == originalWorld && entity.IsAlive() && !runtime.IsPaused(), "Compile failure stopped or replaced the World.");
                const auto calls = managedEntity.IsAlive() ? managedEntity.GetComponent<TransformComponent>()->scale.y : 0.f;
                Require(runtime.Update(10).has_value() && entity.GetComponent<TransformComponent>()->translation.x == 5, "Compile failure did not retain original Native behavior.");
                if(argc == 5) Require(managedEntity.IsAlive() && managedEntity.GetComponent<TransformComponent>()->scale.y == calls + 1, "Compile failure invalidated a working Managed Binding.");
                std::ofstream(signals / "ErrorPreserved") << "original code / World / Binding retained\n";
                std::cout << "COMPILE ERROR VERIFIED: original code retained; submit a valid Request next.\n" << std::flush;
                requested = false; checkingCompileError = false; compileErrorPreserved = true;
                continue;
            }
            if(requested && state == NativeReloadState::Succeeded) {
                Require(!checkingCompileError, "Invalid compile-error fixture unexpectedly produced a successful patch.");
                Require(runtime.GetLevel() == originalWorld && entity.IsAlive() && originalWorld->GetScene()->GetEntityCount() == entityCount, "Patch changed World / Entity identity.");
                const auto managedCalls = managedEntity.IsAlive() ? managedEntity.GetComponent<TransformComponent>()->scale.y : 0.f;
                Require(runtime.Update(10).has_value(), "Patched Native update failed.");
                Require(std::abs(entity.GetComponent<TransformComponent>()->translation.x - 9) < .001f, "Patch was reported successful without new gameplay behavior.");
                if(argc == 5) Require(managedEntity.IsAlive() && managedEntity.GetComponent<TransformComponent>()->translation.x == 3 && managedEntity.GetComponent<TransformComponent>()->scale.y == managedCalls + 1,
                    "Managed cached Binding did not resume after Native patch.");
                runtime.Stop(); Require(!runtime.IsRunning(), "Patched Runtime did not stop.");
                std::ofstream(signals / "Passed") << "same World / Entity, target=9, stopped\n";
                std::cout << "PASS: real Live++ patch, World / Entity preserved, new Native behavior and normal shutdown." << (argc == 5 ? " Managed cached Binding refreshed.\n" : "\n");
                return 0;
            }
            if(requested && (state == NativeReloadState::CompileError || state == NativeReloadState::CompatibilityError || state == NativeReloadState::StateRestoreError || state == NativeReloadState::NoChanges))
                throw std::runtime_error("Native patch did not succeed; inspect the preceding SDK / Engine log.");
            Require(runtime.Update(0).has_value(), "World failed while waiting for Native patch.");
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        throw std::runtime_error("Timed out waiting for Native patch request / completion.");
    } catch(const std::exception& exception) { std::cerr << "FAIL: " << exception.what() << '\n'; return 1; }
}
