#include "ShadowConstants.hlsli"

struct VertexInput
{
    float3 position : POSITION;
    float4 jointIndices : BLENDINDICES;
    float4 jointWeights : BLENDWEIGHT;
};

float4 main(VertexInput input) : SV_POSITION
{
    float4 local = float4(input.position, 1.0f);
    if (shadowDimensions.z != 0)
    {
        const uint4 joints = (uint4)input.jointIndices;
        local = mul(local, jointPalette[joints.x]) * input.jointWeights.x +
            mul(float4(input.position, 1.0f), jointPalette[joints.y]) * input.jointWeights.y +
            mul(float4(input.position, 1.0f), jointPalette[joints.z]) * input.jointWeights.z +
            mul(float4(input.position, 1.0f), jointPalette[joints.w]) * input.jointWeights.w;
    }
    return mul(local, worldViewProjection);
}
