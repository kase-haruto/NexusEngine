#include "../Common/ColorSpace.hlsli"
Texture2D<float4> images[7] : register(t0);
SamplerState linearClamp : register(s0);
cbuffer Settings : register(b0) {
    uint sourceIndex; float exposure; float intensity; float threshold;
    float knee; float saturation; float contrast; float gradingGamma;
    float4 gain; float4 offset; float4 whiteBalance;
    uint toneMapper; float3 padding;
};
struct Vertex { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
Vertex VSMain(uint id : SV_VertexID) {
    Vertex v;
    v.uv = float2((id << 1) & 2, id & 2);
    v.position = float4(v.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return v;
}
float3 Extract(float3 c) {
    float brightness = max(c.r, max(c.g, c.b));
    float k = max(threshold * knee, 1e-5);
    float soft = clamp(brightness - threshold + k, 0, 2 * k);
    soft = soft * soft / (4 * k);
    return c * max(brightness - threshold, soft) / max(brightness, 1e-5);
}
float4 Downsample(Vertex v) : SV_TARGET {
    uint w, h; images[sourceIndex].GetDimensions(w, h);
    float2 texel = 1.0 / float2(w, h);
    float3 sum = 0;
    // Separable tent kernel; extract before averaging so small highlights survive.
    [unroll] for(int y = -1; y <= 1; ++y) {
        [unroll] for(int x = -1; x <= 1; ++x) {
            float3 c = max(images[sourceIndex].SampleLevel(linearClamp, v.uv + float2(x,y) * texel, 0).rgb, 0);
            if(sourceIndex == 0) c = Extract(c);
            sum += c * (x == 0 ? 2 : 1) * (y == 0 ? 2 : 1);
        }
    }
    return float4(min(sum / 16, 65504), 1);
}
// Stephen Hill's ACES fit, from MJP's BakingLab (MIT).
// See ACES-LICENSE.txt in this directory for attribution and license.
// ACES fitted RRT/ODT approximation with input/output colour transforms.
// This is an SDR Rec.709 fit, not the full ACES reference transform or UE's curve.
float3 AcesFit(float3 c) {
    c = mul(float3x3(0.59719,0.35458,0.04823, 0.07600,0.90834,0.01566, 0.02840,0.13383,0.83777), c);
    // Limit before squaring to keep very high exposure finite and tending to white.
    c = min(c, 1e4);
    float3 a = c * (c + 0.0245786) - 0.000090537;
    float3 b = c * (0.983729 * c + 0.4329510) + 0.238081;
    return saturate(mul(float3x3(1.60475,-0.53108,-0.07367, -0.10208,1.10813,-0.00605, -0.00327,-0.07276,1.07602), a / b));
}
// Khronos PBR Neutral mathematical specification, linear Rec.709 in and out.
// https://github.com/KhronosGroup/ToneMapping/tree/main/PBR_Neutral
float3 PbrNeutral(float3 c) {
    float minimum = min(c.r, min(c.g, c.b));
    c -= minimum < 0.08 ? minimum - 6.25 * minimum * minimum : 0.04;
    float peak = max(c.r, max(c.g, c.b));
    if(peak < 0.76) return c;
    float compressedPeak = 1 - 0.24 * 0.24 / (peak - 0.52);
    float3 compressed = c * (compressedPeak / peak);
    float retainedSaturation = 1 / (1 + 0.15 * (peak - compressedPeak));
    return lerp(compressedPeak.xxx, compressed, retainedSaturation);
}
float3 BoundedPower(float3 c, float exponent) {
    // Bound logarithms before exp2, including zero, to avoid NaN/Inf from UI extremes.
    float3 result = exp2(clamp(log2(max(c, 1e-30)) * exponent, -100, 64));
    return float3(c.r > 0 ? result.r : 0, c.g > 0 ? result.g : 0, c.b > 0 ? result.b : 0);
}
float4 Composite(Vertex v) : SV_TARGET {
    float3 c = max(images[0].SampleLevel(linearClamp, v.uv, 0).rgb, 0);
    if(intensity > 0) {
        float3 bloom = 0;
        [unroll] for(uint i = 1; i < 6; ++i) bloom += images[i].SampleLevel(linearClamp, v.uv, 0).rgb * 0.2;
        c += bloom * intensity;
    }
    c *= exposure * whiteBalance.rgb;
    float luma = dot(c, float3(0.2126, 0.7152, 0.0722));
    c = max(lerp(luma.xxx, c, saturation), 0);
    c = 0.18 * BoundedPower(c / 0.18, contrast);
    c = max(BoundedPower(c, 1.0 / gradingGamma) * gain.rgb + offset.rgb, 0);
    if(toneMapper == 1) c = AcesFit(c);
    else if(toneMapper == 2) c = PbrNeutral(c);
    else c = saturate(c);
    c = saturate(c);
    return float4(LinearToSrgb(c), 1);
}

// FXAA in display space: filter along local edges, preserving low-contrast areas.
float4 AntiAliasing(Vertex v) : SV_TARGET {
    uint w, h; images[6].GetDimensions(w, h);
    float2 texel = 1.0 / float2(w, h);
    float3 center = images[6].SampleLevel(linearClamp, v.uv, 0).rgb;
    float3 nw = images[6].SampleLevel(linearClamp, v.uv + float2(-1, -1) * texel, 0).rgb;
    float3 ne = images[6].SampleLevel(linearClamp, v.uv + float2(1, -1) * texel, 0).rgb;
    float3 sw = images[6].SampleLevel(linearClamp, v.uv + float2(-1, 1) * texel, 0).rgb;
    float3 se = images[6].SampleLevel(linearClamp, v.uv + float2(1, 1) * texel, 0).rgb;
    const float3 weights = float3(0.299, 0.587, 0.114);
    float m = dot(center, weights), a = dot(nw, weights), b = dot(ne, weights);
    float c = dot(sw, weights), d = dot(se, weights);
    float lo = min(m, min(min(a, b), min(c, d)));
    float hi = max(m, max(max(a, b), max(c, d)));
    if(hi - lo < max(0.0312, hi * 0.125)) return float4(center, 1);
    float2 direction = float2(-((a + b) - (c + d)), (a + c) - (b + d));
    float reduction = max((a + b + c + d) * (0.25 * 0.125), 1.0 / 128.0);
    direction = clamp(direction / (min(abs(direction.x), abs(direction.y)) + reduction), -8.0, 8.0) * texel;
    float3 inner = 0.5 * (
        images[6].SampleLevel(linearClamp, v.uv - direction / 6.0, 0).rgb +
        images[6].SampleLevel(linearClamp, v.uv + direction / 6.0, 0).rgb);
    float3 outer = inner * 0.5 + 0.25 * (
        images[6].SampleLevel(linearClamp, v.uv - direction * 0.5, 0).rgb +
        images[6].SampleLevel(linearClamp, v.uv + direction * 0.5, 0).rgb);
    float filteredLuma = dot(outer, weights);
    return float4(filteredLuma < lo || filteredLuma > hi ? inner : outer, 1);
}
