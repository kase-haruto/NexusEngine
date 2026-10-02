#ifndef NEXUS_PRIMITIVE_DRAW_DATA_HLSLI
#define NEXUS_PRIMITIVE_DRAW_DATA_HLSLI

// CPU側PrimitiveDrawDataと同じ4256byte layout。row_majorでEngineのrow-vector行列規約を保つ。
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
};

#endif
