#pragma once
#include "SystemScheduler.h"
#include "Scene.h"
#include "Components/AnimationPlayerComponent.h"
#include <limits>
#include <algorithm>

namespace NexusEngine {
class AnimationUpdateSystem final : public ISceneSystem {
public:
    std::span<const std::type_index> GetReadComponents() const noexcept override { return {}; }
    std::span<const std::type_index> GetWriteComponents() const noexcept override {
        static const std::type_index type = typeid(AnimationPlayerComponent);
        return {&type, 1};
    }
    Result<void> Update(Scene& scene, float deltaTime) override {
        scene.ForEach<AnimationPlayerComponent>([deltaTime](Entity, AnimationPlayerComponent& player) {
            if(!player.playing || !player.IsValid()) return;
            const double next = static_cast<double>(player.time) + static_cast<double>(deltaTime) * player.playbackSpeed;
            player.time = static_cast<float>((std::min)(next, static_cast<double>((std::numeric_limits<float>::max)())));
        });
        return {};
    }
};
}
