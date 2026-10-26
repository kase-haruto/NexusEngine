#include "Platform/Development/LiveCodingAgent.h"
#include "Foundation/Logging/Logger.h"
#include "Foundation/Reflection/ReinstancingService.h"
#include "Runtime/Reflection/ComponentReflection.h"
#include "Runtime/Scene/Components/NameComponent.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

using namespace NexusEngine;
namespace {
ReflectionValue Capture(void* context) {
    ComponentReflection registry; RegisterGeneratedComponents(registry);
    return registry.Capture(*static_cast<Scene*>(context));
}
void Release(void* context) noexcept { static_cast<Scene*>(context)->ReleaseComponentStorages(); }
void Restore(void* context, const ReflectionValue& snapshot) {
    auto& scene = *static_cast<Scene*>(context);
    ComponentReflection registry; RegisterGeneratedComponents(registry); registry.Restore(scene, snapshot);
    size_t count = 0;
    scene.ForEach<NameComponent>([&](Entity, const NameComponent& name) {
        if(name.name != "preserved-live-name") throw std::runtime_error("Live name restoration failed");
        ++count;
    });
    if(count != 1) throw std::runtime_error("Live entity count changed");
    std::cout << "PATCH VERIFIED: NameComponent size=" << sizeof(NameComponent) << ", name preserved\n" << std::flush;
}
}
int main() {
    Logger::Get().InitializeDefaultSinks();
    const auto directory = std::filesystem::absolute("Generated/LiveCodingSmoke");
    std::filesystem::create_directories(directory);
    std::filesystem::remove(directory / "Ready");
    std::filesystem::remove(directory / "Request");
    std::filesystem::remove(directory / "Stop");
    try {
        Scene scene; static_cast<void>(scene.CreateEntity("preserved-live-name"));
        auto& service = ReinstancingService::Get();
        service.Register({"SmokeScene", &scene, Capture, Release, Restore});
        LiveCodingAgent agent(true);
        std::cout << "READY: NameComponent size=" << sizeof(NameComponent) << '\n' << std::flush;
        std::ofstream(directory / "Ready") << sizeof(NameComponent);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(5);
        while(std::chrono::steady_clock::now() < deadline && !std::filesystem::exists(directory / "Stop")) {
            // 外部からのファイル要求で SDK を直接呼び、GUI キー入力に依存せず検証する。
            if(std::filesystem::exists(directory / "Request")) {
                std::filesystem::remove(directory / "Request"); agent.ScheduleReload();
                std::cout << "RELOAD SCHEDULED\n" << std::flush;
            }
            if(agent.WantsReload()) agent.Reload();
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        service.Unregister(&scene);
        return service.Generation() ? 0 : 2;
    } catch(const std::exception& exception) { std::cerr << exception.what() << '\n'; return 1; }
}
