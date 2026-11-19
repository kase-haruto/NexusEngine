#include "ShadowConstants.hlsli"

struct VertexInput
{
    float3 position : POSITION;
    float4 jointIndices : BLENDINDICES;
    float4 jointWeights : BLENDWEIGHT;
};

float4 main(VertexInput input, uint vertex : SV_VertexID) : SV_POSITION
{
    float3 position = input.position;
    float3 normal = 0, tangent = 0;
    ApplyMorph(deformation, vertex, position, normal, tangent);
    float4 local = float4(position, 1.0f);
    if (deformation.jointCount != 0)
    {
        const uint4 joints = (uint4)input.jointIndices;
        local = mul(local, DeformationJoint(deformation, joints.x)) * input.jointWeights.x +
            mul(float4(position, 1.0f), DeformationJoint(deformation, joints.y)) * input.jointWeights.y +
            mul(float4(position, 1.0f), DeformationJoint(deformation, joints.z)) * input.jointWeights.z +
            mul(float4(position, 1.0f), DeformationJoint(deformation, joints.w)) * input.jointWeights.w;
    }
    return mul(local, worldViewProjection);
}
