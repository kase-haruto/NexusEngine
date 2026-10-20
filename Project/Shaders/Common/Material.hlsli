#ifndef NEXUS_MATERIAL
#define NEXUS_MATERIAL
// Raster supplies Sample values, DXR supplies SampleLevel values.
float2 MaterialMetallicRoughness(float2 factors, float4 texel) {
    return float2(saturate(factors.x * texel.b), clamp(factors.y * texel.g, 0.05, 1));
}
float MaterialOcclusion(float red, float strength) { return lerp(1, saturate(red), saturate(strength)); }
float3 MaterialNormal(float3 texel, float scale, float3 n, float3 t, float3 b) {
    float3 mapped = texel * 2 - 1; mapped.xy *= scale;
    return SafeNormalize(t * mapped.x + b * mapped.y + n * mapped.z, n);
}
#endif
