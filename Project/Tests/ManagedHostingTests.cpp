#include "Runtime/Application/GameRuntime.h"
#include "Runtime/Gameplay/GameApiBridge.h"
#include "Runtime/Scripting/CSharpRuntime.h"
#include "Foundation/Logging/BufferedLogSink.h"
#include "Foundation/Logging/Logger.h"
#include <Windows.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

using namespace NexusEngine;
namespace {
void Require(bool condition, const char* message) { if(!condition) throw std::runtime_error(message); }
std::unique_ptr<Level> Empty() {
    auto level = std::make_unique<Level>();
    auto loaded = level->LoadEmpty(); if(!loaded) throw std::runtime_error(loaded.error().GetMessageText());
    return level;
}
void Started(GameRuntime& runtime) {
    auto result = runtime.Start(Empty());
    if(!result) throw std::runtime_error(result.error().GetMessageText());
}
}

/** \brief ユーザーが明示的にBuild・実行する公式Hosting統合テスト。通常のNativeテストへ.NET依存を追加しない */
int main() {
    try {
        std::wstring executable(32768, L'\0');
        const auto size = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        Require(size && size < executable.size(), "Cannot locate test host."); executable.resize(size);
        const auto directory = std::filesystem::path(executable).parent_path();
        const CSharpRuntimeSettings settings{directory / "nethost.dll", directory / "Managed/Nexus.Managed.Bootstrap.runtimeconfig.json",
            directory / "Managed/Nexus.Managed.Bootstrap.dll", {}};
        auto buffer = std::make_shared<BufferedLogBuffer>();
        auto sink = std::make_unique<BufferedLogSink>(buffer); const auto* observer = sink.get();
        Require(Logger::Get().AddSink(std::move(sink)), "Cannot register Managed log observer.");
        // Runtimeより長くObserverを保持し、終了ログも同じ共通Loggerから取得する。
        struct RemoveObserver { const ILogSink* observedSink; ~RemoveObserver() { static_cast<void>(Logger::Get().RemoveSink(observedSink)); } } remove{observer};
        {
            GameRuntime first, second;
            Require(first.SetCSharpRuntime(settings) && second.SetCSharpRuntime(settings), "Cannot configure Managed sessions.");
            Started(first); Started(second);
            Require(first.GetLevel()->GetScene()->GetEntityCount() == 0, "Phase 3 Bootstrap unexpectedly created gameplay entities.");
            Require(first.Update(.1f).has_value(), "Existing World update failed while hosting Managed code.");
            first.TogglePause(); Require(first.Update(.1f).has_value(), "Paused World update failed.");
            first.Stop(); first.Stop(); second.Stop();
            Started(first); first.Stop(); // 同じProcessのCoreCLRを使った新セッションの開始を確認する。
        }
        const auto logs = buffer->Snapshot();
        Require(std::ranges::count_if(logs, [](const auto& record) { return record.message == "Managed bootstrap started."; }) == 3,
            "Managed entry was not called once per session.");
        Require(std::ranges::count_if(logs, [](const auto& record) { return record.message == "Managed bootstrap stopped."; }) == 3,
            "Managed sessions were not shut down once each.");
        {
            auto level = Empty(); GameApiBridge world(*level);
            DotNetHost host; auto initialized = host.Initialize(settings);
            if(!initialized) throw std::runtime_error(initialized.error().GetMessageText());
            Require(!host.Resolve(settings.bootstrapAssembly, L"MissingMethod"), "Missing Managed entry was accepted.");
            bool wrongThreadRejected = false;
            std::thread worker([&] { wrongThreadRejected = !host.Resolve(settings.bootstrapAssembly, L"Start"); }); worker.join();
            Require(wrongThreadRejected, "Managed entry resolution accepted a foreign thread.");
            NativeManagedBridge bridge;
            const NexusManagedBootstrapContextV1 invalid{999, sizeof(NexusManagedBootstrapContextV1), world.World(), GameApiBridge::Api().log};
            Require(!bridge.Start(host, settings.bootstrapAssembly, invalid), "Managed Bootstrap accepted an invalid ABI.");
            Require(bridge.Stop(), "Invalid Bootstrap start left partial state.");
        }
        std::cout << "PASS: official Hosting, Managed entry calls, session isolation, repeat start/stop, shared Logger, missing entry and ABI/thread rejection.\n";
        return 0;
    } catch(const std::exception& exception) { std::cerr << "FAIL: " << exception.what() << '\n'; return 1; }
}
