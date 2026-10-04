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
    if (opticalRadianceIndex != 0)
    {
        Texture2D<float4> radiance = ResourceDescriptorHeap[opticalRadianceIndex];
        const float4 optical = radiance.Load(int3(uint2(input.position.xy), 0));
        if (optical.a > 0.5) return float4(optical.rgb, 1);
    }
    Texture2D<float4> materialTexture = NexusGetTexture2D(textureIndex);
    SamplerState materialSampler = NexusGetSampler(samplerIndex);
    const float3 lightDirection = normalize(lightDirectionIntensity.xyz);
    float visibility = 1.0f;
    if (shadowMaskIndex != 0)
    {
        // Lighting consumes visibility only; it never branches on RT vs Shadow Map.
        Texture2D<float> shadowMask = ResourceDescriptorHeap[shadowMaskIndex];
        visibility = shadowMask.Load(int3(uint2(input.position.xy), 0));
    }
    const float3 lighting = 1.0f + lightColor.rgb * lightDirectionIntensity.w *
        saturate(dot(normalize(input.worldNormal), lightDirection)) * visibility;
    const float4 albedo = input.color * materialTexture.Sample(materialSampler, input.texcoord) * materialTint;
    return float4(albedo.rgb * lighting, albedo.a);
}
