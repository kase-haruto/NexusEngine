#include "ShadowConstants.hlsli"

// Compute shaderではddx/ddyを使えないため、深度の連続する側から受光面を復元する。
// シルエットで背景や別の面をまたぐ差分を選ばず、画面端でも範囲外Loadをしない。
float3 ReceiverDelta(Texture2D<float> surfaceDepth, uint2 pixel, int2 axis, float depth, float3 world)
{
    const int2 before = int2(pixel) - axis;
    const int2 after = int2(pixel) + axis;
    const bool hasBefore = all(before >= 0) && all(before < int2(shadowDimensions.xy));
    const bool hasAfter = all(after >= 0) && all(after < int2(shadowDimensions.xy));
    const float beforeDepth = hasBefore ? surfaceDepth.Load(int3(before, 0)) : 1.0f;
    const float afterDepth = hasAfter ? surfaceDepth.Load(int3(after, 0)) : 1.0f;
    const bool useBefore = beforeDepth < 1.0f &&
        (afterDepth >= 1.0f || abs(beforeDepth - depth) < abs(afterDepth - depth));
    if (useBefore)
    {
        return world - ReconstructShadowSurface(uint2(before), beforeDepth);
    }
    if (afterDepth < 1.0f)
    {
        return ReconstructShadowSurface(uint2(after), afterDepth) - world;
    }
    return 0;
}

// 正射影のlight UVに対する深度勾配。各PCF tapの中心に同じ受光平面を延長する。
// 固定Biasを大きくすると接地影が浮くため、面の傾きによる深度差だけを補正する。
float2 ReceiverDepthGradient(Texture2D<float> surfaceDepth, uint2 pixel, float depth, float3 world)
{
    const float3 dx = mul(float4(ReceiverDelta(surfaceDepth, pixel, int2(1, 0), depth, world), 0), lightViewProjection).xyz;
    const float3 dy = mul(float4(ReceiverDelta(surfaceDepth, pixel, int2(0, 1), depth, world), 0), lightViewProjection).xyz;
    const float2 duvX = dx.xy * float2(.5f, -.5f);
    const float2 duvY = dy.xy * float2(.5f, -.5f);
    const float determinant = duvX.x * duvY.y - duvX.y * duvY.x;
    // 光に対して退化した面では逆行列を作らず、既存の固定Biasに戻す。
    if (abs(determinant) <= 1.0e-5f * length(duvX) * length(duvY) || determinant == 0)
    {
        return 0;
    }
    return float2(duvY.y * dx.z - duvX.y * dy.z, duvX.x * dy.z - duvY.x * dx.z) / determinant;
}

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
            const float2 texelPosition = uv * float2(width, height) - 0.5f;
            const int2 texel = int2(floor(texelPosition));
            const float2 fraction = frac(texelPosition);
            const float2 depthGradient = ReceiverDepthGradient(surfaceDepth, thread.xy, depth, world);
            visibility = 0.0f;
            // Bilinear comparison filtering makes the PCF footprint continuous as
            // receivers move between texels, without a comparison sampler allocation.
            // Out-of-volume samples are lit, matching the map's limited orthographic coverage.
            // Merge overlapping bilinear taps of the 3x3 kernel into 4x4 loads.
            for (int y = -1; y <= 2; ++y)
            for (int x = -1; x <= 2; ++x)
            {
                const int2 tap = texel + int2(x, y);
                const float weight = (x == -1 ? 1 - fraction.x : (x == 2 ? fraction.x : 1)) *
                                     (y == -1 ? 1 - fraction.y : (y == 2 ? fraction.y : 1));
                const float2 tapUv = (float2(tap) + .5f) / float2(width, height);
                const float receiverDepth = ndc.z + dot(depthGradient, tapUv - uv);
                visibility += weight * (any(tap < 0) || any(tap >= int2(width, height)) ? 1.0f :
                    (receiverDepth - shadowBiases.w <= lightDepth.Load(int3(tap, 0)) ? 1.0f : 0.0f));
            }
            // 重みの総和は9だが、浮動小数点の丸めで1を僅かに超えないようにする。
            visibility = saturate(visibility / 9.0f);
        }
    }
    mask[thread.xy] = visibility;
}
