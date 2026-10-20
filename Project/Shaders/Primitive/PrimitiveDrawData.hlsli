#ifndef NEXUS_PRIMITIVE_DRAW_DATA_HLSLI
#define NEXUS_PRIMITIVE_DRAW_DATA_HLSLI

// CPU側PrimitiveDrawDataと同じ4720byte配置。行列はrow-vector規約。
#include "../Common/IndirectLighting.hlsli"
#include "../Common/BakedLighting.hlsli"
#include "../Common/DDGI.hlsli"
cbuffer PrimitiveDrawData : register(b0)
{
    row_major float4x4 worldViewProjection;
    row_major float4x4 worldInverseTranspose;
    uint textureIndex;
    uint samplerIndex;
    uint skinningEnabled;
    float drawDataPadding;
    float4 materialTint;
    row_major float4x4 jointPalette[64];
    uint shadowMaskIndex;
    uint3 shadowPadding;
    float4 lightDirectionIntensity;
    float4 lightColor;
    uint opticalRadianceIndex;
    uint diffuseGIIndex;
    uint2 opticalPadding;
    row_major float4x4 world;
    float4 cameraPosition;
    float4 ambientIrradiance;
    float4 pbrParameters;
    uint normalTextureIndex; uint normalSamplerIndex;
    uint metallicRoughnessTextureIndex; uint metallicRoughnessSamplerIndex;
    float4 fogColorEnabled;
    float4 fogDistances;
    float4 emissiveOcclusion;
    uint4 extraMaps;
    EnvironmentData environment;
    BakedLightingData bakedLighting;
    DDGIData ddgi;
};

#endif
