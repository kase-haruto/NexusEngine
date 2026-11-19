#include "PrimitiveDrawData.hlsli"

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD;
    float4 color : COLOR;
    float4 jointIndices : BLENDINDICES;
    float4 jointWeights : BLENDWEIGHT;
    float4 tangent : TANGENT;
    float2 lightmapUV : TEXCOORD1;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 worldNormal : NORMAL;
    float2 texcoord : TEXCOORD;
    float4 color : COLOR;
    float3 worldPosition : TEXCOORD3;
    float4 worldTangent : TANGENT;
    float2 lightmapUV : TEXCOORD4;
};

VSOutput main(VSInput input, uint vertex : SV_VertexID)
{
    VSOutput output;
    float3 morphedPosition = input.position;
    float3 morphedNormal = input.normal;
    float3 morphedTangent = input.tangent.xyz;
    ApplyMorph(deformation, vertex, morphedPosition, morphedNormal, morphedTangent);
    float4 localPosition = float4(morphedPosition, 1.0f);
    float3 localNormal = morphedNormal;
    float3 localTangent = morphedTangent;
    if (skinningEnabled != 0)
    {
        const uint4 joints = (uint4)input.jointIndices;
        localPosition =
            mul(float4(morphedPosition, 1.0f), DeformationJoint(deformation, joints.x)) * input.jointWeights.x +
            mul(float4(morphedPosition, 1.0f), DeformationJoint(deformation, joints.y)) * input.jointWeights.y +
            mul(float4(morphedPosition, 1.0f), DeformationJoint(deformation, joints.z)) * input.jointWeights.z +
            mul(float4(morphedPosition, 1.0f), DeformationJoint(deformation, joints.w)) * input.jointWeights.w;
        localNormal =
            mul(morphedNormal, (float3x3)DeformationJoint(deformation, joints.x)) * input.jointWeights.x +
            mul(morphedNormal, (float3x3)DeformationJoint(deformation, joints.y)) * input.jointWeights.y +
            mul(morphedNormal, (float3x3)DeformationJoint(deformation, joints.z)) * input.jointWeights.z +
            mul(morphedNormal, (float3x3)DeformationJoint(deformation, joints.w)) * input.jointWeights.w;
        localTangent =
            mul(morphedTangent, (float3x3)DeformationJoint(deformation, joints.x)) * input.jointWeights.x +
            mul(morphedTangent, (float3x3)DeformationJoint(deformation, joints.y)) * input.jointWeights.y +
            mul(morphedTangent, (float3x3)DeformationJoint(deformation, joints.z)) * input.jointWeights.z +
            mul(morphedTangent, (float3x3)DeformationJoint(deformation, joints.w)) * input.jointWeights.w;
    }
    output.position = mul(localPosition, worldViewProjection);
    output.worldNormal = normalize(mul(localNormal, (float3x3)worldInverseTranspose));
    output.worldPosition = mul(localPosition, world).xyz;
    output.worldTangent = float4(mul(localTangent, (float3x3)world),
        input.tangent.w * (determinant((float3x3)world) < 0 ? -1 : 1));
    output.texcoord = input.texcoord;
    output.lightmapUV = input.lightmapUV;
    output.color = input.color;
    return output;
}
