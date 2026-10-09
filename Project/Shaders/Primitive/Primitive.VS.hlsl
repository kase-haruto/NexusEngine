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
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 worldNormal : NORMAL;
    float2 texcoord : TEXCOORD;
    float4 color : COLOR;
    float3 worldPosition : TEXCOORD3;
    float4 worldTangent : TANGENT;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    float4 localPosition = float4(input.position, 1.0f);
    float3 localNormal = input.normal;
    float3 localTangent = input.tangent.xyz;
    if (skinningEnabled != 0)
    {
        const uint4 joints = (uint4)input.jointIndices;
        localPosition =
            mul(float4(input.position, 1.0f), jointPalette[joints.x]) * input.jointWeights.x +
            mul(float4(input.position, 1.0f), jointPalette[joints.y]) * input.jointWeights.y +
            mul(float4(input.position, 1.0f), jointPalette[joints.z]) * input.jointWeights.z +
            mul(float4(input.position, 1.0f), jointPalette[joints.w]) * input.jointWeights.w;
        localNormal =
            mul(input.normal, (float3x3)jointPalette[joints.x]) * input.jointWeights.x +
            mul(input.normal, (float3x3)jointPalette[joints.y]) * input.jointWeights.y +
            mul(input.normal, (float3x3)jointPalette[joints.z]) * input.jointWeights.z +
            mul(input.normal, (float3x3)jointPalette[joints.w]) * input.jointWeights.w;
        localTangent =
            mul(input.tangent.xyz, (float3x3)jointPalette[joints.x]) * input.jointWeights.x +
            mul(input.tangent.xyz, (float3x3)jointPalette[joints.y]) * input.jointWeights.y +
            mul(input.tangent.xyz, (float3x3)jointPalette[joints.z]) * input.jointWeights.z +
            mul(input.tangent.xyz, (float3x3)jointPalette[joints.w]) * input.jointWeights.w;
    }
    output.position = mul(localPosition, worldViewProjection);
    output.worldNormal = normalize(mul(localNormal, (float3x3)worldInverseTranspose));
    output.worldPosition = mul(localPosition, world).xyz;
    output.worldTangent = float4(mul(localTangent, (float3x3)world),
        input.tangent.w * (determinant((float3x3)world) < 0 ? -1 : 1));
    output.texcoord = input.texcoord;
    output.color = input.color;
    return output;
}
