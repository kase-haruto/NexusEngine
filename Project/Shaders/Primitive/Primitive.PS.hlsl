#include "../Common/Bindless.hlsli"
#include "PrimitiveDrawData.hlsli"

struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldNormal : NORMAL;
    float2 texcoord : TEXCOORD;
    float4 color : COLOR;
};

float4 main(PSInput input) : SV_TARGET
{
    Texture2D<float4> materialTexture = NexusGetTexture2D(textureIndex);
    SamplerState materialSampler = NexusGetSampler(samplerIndex);
    const float3 lightDirection = normalize(float3(-0.4f, 0.8f, -0.6f));
    const float lighting = 0.25f + 0.75f * saturate(dot(normalize(input.worldNormal), lightDirection));
    const float4 albedo = input.color * materialTexture.Sample(materialSampler, input.texcoord) * materialTint;
    return float4(albedo.rgb * lighting, albedo.a);
}
