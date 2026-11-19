#pragma once
#include <cstdint>

namespace NexusEngine {
/** \brief Forward/Shadow/Selectionで共有するbyte-address bufferの描画契約。HLSLと32byte一致。 */
struct DeformationData {
    uint32_t poseBuffer = 0, jointCount = 0, weightOffset = 0, weightCount = 0;
    uint32_t morphBuffer = 0, vertexCount = 0, targetCount = 0, padding = 0;
};
static_assert(sizeof(DeformationData) == 32);
}
