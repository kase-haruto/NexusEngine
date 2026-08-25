#include "../Common/Bindless.hlsli"

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

// CPU側MaterialDrawDataと同じ32byte layout。Descriptor handleではなくHeap indexだけを受け取る。
cbuffer PrimitiveMaterial : register(b0)
{
    uint textureIndex;
    uint samplerIndex;
    float2 materialPadding;
    float4 materialTint;
};

float4 main(PSInput input) : SV_TARGET
{
    Texture2D<float4> materialTexture = NexusGetTexture2D(textureIndex);
    SamplerState materialSampler = NexusGetSampler(samplerIndex);
    // 検証用Textureは1x1なので固定UVでも全pixelで同じwhite texelを取得する。
    return input.color * materialTexture.Sample(materialSampler, float2(0.5f, 0.5f)) * materialTint;
}
