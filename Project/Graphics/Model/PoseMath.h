#pragma once
#include "Foundation/Math/Quaternion.h"
namespace NexusEngine {
/** \brief 姿勢とRoot Motionで共通の最短経路Quaternion補間。 */
Quaternion BlendPoseRotation(Quaternion first, Quaternion second, float weight) noexcept;
}
