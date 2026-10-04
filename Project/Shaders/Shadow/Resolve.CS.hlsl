#include "ShadowConstants.hlsli"

[numthreads(8, 8, 1)]
void main(uint3 thread : SV_DispatchThreadID)
{
    if (any(thread.xy >= shadowDimensions.xy)) return;
    Texture2D<float> surfaceDepth = ResourceDescriptorHeap[shadowIndices.x];
    Texture2D<float> lightDepth = ResourceDescriptorHeap[shadowIndices.y];
    RWTexture2D<float> mask = ResourceDescriptorHeap[shadowIndices.z];
    const float depth = surfaceDepth.Load(int3(thread.xy, 0));
    float visibility = 1.0f;
    if (depth < 1.0f)
    {
        const float3 world = ReconstructShadowSurface(thread.xy, depth);
        const float4 clip = mul(float4(world, 1.0f), lightViewProjection);
        const float3 ndc = clip.xyz / clip.w;
        const float2 uv = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
        if (all(uv >= 0.0f) && all(uv <= 1.0f) && ndc.z >= 0.0f && ndc.z <= 1.0f)
        {
            uint width, height;
            lightDepth.GetDimensions(width, height);
            const int2 texel = int2(uv * float2(width, height));
            visibility = 0.0f;
            // Small fixed PCF footprint avoids an additional comparison sampler allocation.
            // Out-of-volume samples are lit, matching the map's limited orthographic coverage.
            for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x)
            {
                const int2 tap = texel + int2(x, y);
                visibility += any(tap < 0) || any(tap >= int2(width, height)) ? 1.0f :
                    (ndc.z - shadowBiases.w <= lightDepth.Load(int3(tap, 0)) ? 1.0f : 0.0f);
            }
            visibility /= 9.0f;
        }
    }
    mask[thread.xy] = visibility;
}
