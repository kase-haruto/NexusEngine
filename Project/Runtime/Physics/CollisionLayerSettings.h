#pragma once
#include "PhysicsWorld.h"
#include <filesystem>
#include <string>

namespace NexusEngine {
/** Project共有の名前・対称な衝突ルール。削除済みIDは移行先を保持し、旧Levelの所属を安全に解決する。 */
struct CollisionLayerSettings {
    CollisionLayerSettings();
    std::array<std::string, 32> names;
    std::array<uint32_t, 32> redirects;
    CollisionMatrix matrix;
    bool IsValid() const noexcept;
    uint32_t Resolve(uint32_t layer) const noexcept;
    std::string Label(uint32_t layer) const;
    CollisionMatrix EffectiveMatrix() const noexcept;
    static Result<CollisionLayerSettings> Load(const std::filesystem::path& path);
    Result<void> Save(const std::filesystem::path& path) const;
    friend bool operator==(const CollisionLayerSettings&, const CollisionLayerSettings&) = default;
};
}
