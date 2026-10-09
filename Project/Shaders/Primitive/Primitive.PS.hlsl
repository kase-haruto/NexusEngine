#include "../Common/Bindless.hlsli"
#include "PrimitiveDrawData.hlsli"
#include "../Common/PBR.hlsli"
#include "../Common/Fog.hlsli"

struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldNormal : NORMAL;
    float2 texcoord : TEXCOORD;
    float4 color : COLOR;
    float3 worldPosition : TEXCOORD3;
    float4 worldTangent : TANGENT;
};

float3 SurfaceNormal(PSInput input) {
    float3 n = SafeNormalize(input.worldNormal, float3(0, 1, 0));
    if(normalTextureIndex == 0) return n;
    Texture2D<float4> map = NexusGetTexture2D(normalTextureIndex);
    SamplerState sampleState = NexusGetSampler(normalSamplerIndex);
    float3 mapped = map.Sample(sampleState, input.texcoord).xyz * 2 - 1;
    mapped.xy *= pbrParameters.z;
    float3 t, b;
    if(abs(input.worldTangent.w) > 0.5 && dot(input.worldTangent.xyz, input.worldTangent.xyz) > 1e-12) {
        t = SafeNormalize(input.worldTangent.xyz - n * dot(n, input.worldTangent.xyz), float3(1, 0, 0));
        b = cross(n, t) * input.worldTangent.w;
    } else {
        // Tangent-free assets: signed UV derivatives also handle mirrored islands.
        float3 dpdx = ddx(input.worldPosition), dpdy = ddy(input.worldPosition);
        float2 duvdx = ddx(input.texcoord), duvdy = ddy(input.texcoord);
        float det = duvdx.x * duvdy.y - duvdx.y * duvdy.x;
        if(abs(det) < 1e-10) return n;
        t = (dpdx * duvdy.y - dpdy * duvdx.y) / det;
        b = (dpdy * duvdx.x - dpdx * duvdy.x) / det;
        t = SafeNormalize(t - n * dot(n, t), float3(1, 0, 0));
        b = cross(n, t) * (dot(cross(n, t), b) < 0 ? -1 : 1);
    }
    return SafeNormalize(t * mapped.x + b * mapped.y + n * mapped.z, n);
}

float4 main(PSInput input) : SV_TARGET
{
    if (opticalRadianceIndex != 0)
    {
        Texture2D<float4> radiance = ResourceDescriptorHeap[opticalRadianceIndex];
        const float4 optical = radiance.Load(int3(uint2(input.position.xy), 0));
        if (optical.a > 0.5) return float4(clamp(ApplyDistanceFog(optical.rgb, input.worldPosition), 0, 65504), 1);
    }
    Texture2D<float4> materialTexture = NexusGetTexture2D(textureIndex);
    SamplerState materialSampler = NexusGetSampler(samplerIndex);
    const float3 lightDirection = SafeNormalize(lightDirectionIntensity.xyz, float3(0, 1, 0));
    float visibility = 1.0f;
    if (shadowMaskIndex != 0)
    {
        // Lighting consumes visibility only; it never branches on RT vs Shadow Map.
        Texture2D<float> shadowMask = ResourceDescriptorHeap[shadowMaskIndex];
        visibility = shadowMask.Load(int3(uint2(input.position.xy), 0));
    }
    const float4 albedo = input.color * materialTexture.Sample(materialSampler, input.texcoord) * materialTint;
    float metallic = pbrParameters.x, roughness = pbrParameters.y;
    if(metallicRoughnessTextureIndex != 0) {
        Texture2D<float4> map = NexusGetTexture2D(metallicRoughnessTextureIndex);
        SamplerState sampleState = NexusGetSampler(metallicRoughnessSamplerIndex);
        float4 mr = map.Sample(sampleState, input.texcoord);
        roughness *= mr.g; metallic *= mr.b;
    }
    metallic = saturate(metallic); roughness = clamp(roughness, 0.05, 1);
    float3 n = SurfaceNormal(input);
    float3 v = SafeNormalize(cameraPosition.xyz - input.worldPosition, n);
    float3 baseColor = saturate(albedo.rgb);
    float3 radiance = EvaluatePBR(baseColor, metallic, roughness, n, v, lightDirection) *
        lightColor.rgb * max(lightDirectionIntensity.w, 0) * saturate(visibility);
    radiance += ambientIrradiance.rgb * baseColor * (1 - metallic) * (1 - 0.04) / NexusPi;
    // Preserve HDR values above 1 while avoiding half-float overflow in the scene target.
    return float4(clamp(ApplyDistanceFog(radiance, input.worldPosition), 0, 65504), albedo.a);
}
