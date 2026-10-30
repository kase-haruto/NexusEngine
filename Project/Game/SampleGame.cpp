#include "SampleGame.h"
#include "Components/MoveComponent.h"
#include "Runtime/Scene/Components/NameComponent.h"
#include "Runtime/Scene/Components/TransformComponent.h"
#include <array>

namespace NexusGame {
namespace {
/** MoveComponentを付けた個体だけを移動させるゲーム側のSystem。 */
class MoveSystem final : public NexusEngine::ISceneSystem {
public:
    std::span<const std::type_index> GetReadComponents() const noexcept override {
        static const std::array types{std::type_index(typeid(MoveComponent))}; return types;
    }
    std::span<const std::type_index> GetWriteComponents() const noexcept override {
        static const std::array types{std::type_index(typeid(NexusEngine::TransformComponent))}; return types;
    }
    NexusEngine::Result<void> Update(NexusEngine::Scene& scene, float deltaTime) override {
        scene.ForEach<MoveComponent, NexusEngine::TransformComponent>(
            [deltaTime](NexusEngine::Entity, const MoveComponent& movement, NexusEngine::TransformComponent& transform) {
                transform.translation.x += movement.speed * deltaTime;
            });
        return {};
    }
};
/* サンプルLevelのRuntimeSpinnerだけを回す。Engineや既存の背景Levelには規則を追加しない。 */
class SpinnerSystem final : public NexusEngine::ISceneSystem {
public:
    std::span<const std::type_index> GetReadComponents() const noexcept override {
        static const std::array types{std::type_index(typeid(NexusEngine::NameComponent))};
        return types;
    }
    std::span<const std::type_index> GetWriteComponents() const noexcept override {
        static const std::array types{std::type_index(typeid(NexusEngine::TransformComponent))};
        return types;
    }
    NexusEngine::Result<void> Update(NexusEngine::Scene& scene, float deltaTime) override {
        using namespace NexusEngine;
        const auto rotation = Quaternion::FromEulerRadians({0, deltaTime, 0});
        scene.ForEach<NameComponent, TransformComponent>([&](Entity, const NameComponent& name, TransformComponent& transform) {
            if(name.name == "RuntimeSpinner")
                transform.rotation = Quaternion::Multiply(transform.rotation, rotation).Normalized();
        });
        return {};
    }
};
}
NexusEngine::Result<void> SetupSampleGame(NexusEngine::Level& level) {
    level.GetSystemScheduler().AddSystem<SpinnerSystem>();
    level.GetSystemScheduler().AddSystem<MoveSystem>();
    return {};
}
}
