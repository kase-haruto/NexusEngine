#pragma once
#include <cstdint>

namespace NexusEngine {
// Asset preserves the existing model-file path. Built-in shapes use unit dimensions.
enum class PrimitiveShape : uint32_t { Asset, Cube, Sphere, Plane, Cylinder, Cone };
[[nodiscard]] constexpr bool IsValidPrimitiveShape(PrimitiveShape shape) noexcept {
    return shape >= PrimitiveShape::Asset && shape <= PrimitiveShape::Cone;
}
}
