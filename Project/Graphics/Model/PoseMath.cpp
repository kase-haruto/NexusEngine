#include "PoseMath.h"
#include <algorithm>
#include <cmath>
namespace NexusEngine {
Quaternion BlendPoseRotation(Quaternion first, Quaternion second, float weight) noexcept {
    first = first.Normalized();
    second = second.Normalized();
    float dot = first.x * second.x + first.y * second.y + first.z * second.z + first.w * second.w;
    if(dot < 0) {
        dot = -dot;
        second = {-second.x, -second.y, -second.z, -second.w};
    }
    if(dot > .9995f) {
        return Quaternion{first.x + (second.x - first.x) * weight, first.y + (second.y - first.y) * weight,
            first.z + (second.z - first.z) * weight, first.w + (second.w - first.w) * weight}.Normalized();
    }
    const float angle = std::acos(std::clamp(dot, -1.0f, 1.0f));
    const float sine = std::sin(angle);
    const float firstWeight = std::sin((1 - weight) * angle) / sine;
    const float secondWeight = std::sin(weight * angle) / sine;
    return Quaternion{first.x * firstWeight + second.x * secondWeight, first.y * firstWeight + second.y * secondWeight,
        first.z * firstWeight + second.z * secondWeight, first.w * firstWeight + second.w * secondWeight}.Normalized();
}
}
