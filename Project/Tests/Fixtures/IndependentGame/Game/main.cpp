#include "Runtime/Application/GameRuntime.h"
#include "Runtime/Reflection/ComponentReflection.h"
#include "Runtime/Reflection/EngineGenerated/GeneratedReflection.h"
#include "Runtime/Scene/Components/TransformComponent.h"
#include "Game/Generated/GeneratedGameReflection.h"
#include <array>
#include <iostream>

namespace IndependentGame {
/** Gameの型登録はGameホストが組み立て、Engineはこの関数を知らない。 */
void Register(NexusEngine::ComponentReflection& registry) {
    NexusEngine::RegisterGeneratedComponents(registry);
    NexusEngine::RegisterGameComponents(registry);
}
class Movement final : public NexusEngine::ISceneSystem {
public:
    std::span<const std::type_index> GetReadComponents() const noexcept override {
        static const std::array types{std::type_index(typeid(Speed))}; return types;
    }
    std::span<const std::type_index> GetWriteComponents() const noexcept override {
        static const std::array types{std::type_index(typeid(NexusEngine::TransformComponent))}; return types;
    }
    NexusEngine::Result<void> Update(NexusEngine::Scene& scene, float deltaTime) override {
        scene.ForEach<Speed, NexusEngine::TransformComponent>([&](NexusEngine::Entity, const Speed& speed, auto& transform) {
            transform.translation.x += speed.value * deltaTime;
        });
        return {};
    }
};
}

int wmain(int argc, wchar_t** argv) {
    using namespace NexusEngine;
    if(argc != 2) return 1;
    const ReflectionRegistration reflection{IndependentGame::Register, nullptr};
    Level authored("Independent", reflection);
    if(!authored.LoadEmpty()) return 2;
    auto entity = authored.CreateEntity("Mover");
    const auto id = entity.GetComponent<PersistentIdComponent>()->value;
    if(!entity.AddComponent<IndependentGame::Speed>() || !authored.Save(argv[1])) return 3;
    auto loaded = std::make_unique<Level>("Loaded", reflection);
    if(!loaded->Load(argv[1])) return 4;
    GameRuntime runtime;
    if(!runtime.SetLevelSetup([](Level& level) -> Result<void> { level.GetSystemScheduler().AddSystem<IndependentGame::Movement>(); return {}; })) return 5;
    if(!runtime.Start(std::move(loaded)) || !runtime.Update(0.5f)) return 6;
    if(runtime.GetLevel()->FindEntity(id).GetComponent<TransformComponent>()->translation.x != 1) return 7;
    runtime.Stop();
    std::cout << "PASS: independent Game, static Engine API, custom component persistence and GameRuntime update.\n";
    return 0;
}
