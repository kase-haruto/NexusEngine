#ifndef NEXUS_SHADOW_CONSTANTS
#define NEXUS_SHADOW_CONSTANTS
// Matches ShadowGpuConstants (4352 bytes). All matrices use Engine row-vector convention.
cbuffer ShadowConstants : register(b0)
{
    row_major float4x4 worldViewProjection;
    row_major float4x4 inverseViewProjection;
    row_major float4x4 lightViewProjection;
    float4 lightDirectionAngularRadius;
    float4 shadowBiases;
    uint4 shadowIndices;
    uint4 shadowDimensions;
    row_major float4x4 jointPalette[64];
};

float3 ReconstructShadowSurface(uint2 pixel, float depth)
{
    const float2 uv = (float2(pixel) + 0.5f) / float2(shadowDimensions.xy);
    const float4 clip = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f);
    const float4 world = mul(clip, inverseViewProjection);
    return world.xyz / world.w;
}
#endif
