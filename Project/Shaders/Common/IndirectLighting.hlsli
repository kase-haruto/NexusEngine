#ifndef NEXUS_INDIRECT_LIGHTING
#define NEXUS_INDIRECT_LIGHTING
#include "SurfaceData.hlsli"
#include "BRDF.hlsli"
struct EnvironmentData { uint4 textures; float4 parameters; uint4 options; };
float3 EnvironmentDirection(float3 d, EnvironmentData e) {
    return float3(e.parameters.z * d.x - e.parameters.y * d.z, d.y, e.parameters.y * d.x + e.parameters.z * d.z);
}
float3 SampleEnvironment(float3 direction, EnvironmentData e) {
    if(e.options.y == 0) return 0;
    TextureCube<float4> map = ResourceDescriptorHeap[e.textures.x];
    SamplerState sampleState = SamplerDescriptorHeap[e.options.x];
    return map.SampleLevel(sampleState, EnvironmentDirection(direction, e), 0).rgb * e.parameters.x;
}
struct IndirectLighting { float3 diffuse; float3 specular; };
// Texture-free split-sum fit for the uniform fallback sky (E = pi * L).
float2 FallbackEnvironmentBRDF(float nv, float roughness) {
    float4 r = roughness * float4(-1, -.0275, -.572, .022) + float4(1, .0425, 1.04, -.04);
    float a = min(r.x * r.x, exp2(-9.28 * nv)) * r.x + r.y;
    return float2(-1.04, 1.04) * a + r.zw;
}
// E/πを持つ照明供給元で受光材質の適用を共有する。AO・Fresnel・強度は一度だけ掛ける。
float4 ApplyDiffuseIrradiance(float4 irradiance,SurfaceData s,float3 v,EnvironmentData e,float intensity) {
    if(irradiance.a<.5) return 0;
    float3 fresnel=.04;
    if(e.options.y!=0) {
        Texture2D<float2> lut=ResourceDescriptorHeap[e.textures.w];SamplerState state=SamplerDescriptorHeap[e.options.x];
        float2 ab=lut.SampleLevel(state,float2(saturate(dot(s.normal,v)),s.roughness),0);
        fresnel=saturate(lerp(.04.xxx,s.baseColor,s.metallic)*ab.x+ab.y);
    }
    return float4(irradiance.rgb*s.baseColor*(1-s.metallic)*(1-fresnel)*s.ambientOcclusion*intensity,irradiance.a);
}
IndirectLighting EvaluateIndirectLighting(SurfaceData s, float3 v, EnvironmentData e, float3 fallback, float4 tracedDiffuse = 0) {
    IndirectLighting result = (IndirectLighting)0;
    // alpha=0は未計算、1～2は有効なGIと鏡面環境の可視率を表す。
    // 拡散IBL表示は比較のため置換前の結果を維持する。
    const bool useGI = tracedDiffuse.a > 0.5 && e.options.z != 3;
    if(e.options.y == 0) {
        float nv = saturate(dot(s.normal, v));
        float2 ab = FallbackEnvironmentBRDF(nv, s.roughness);
        float3 f = saturate(lerp(.04.xxx, s.baseColor, s.metallic) * ab.x + ab.y);
        result.diffuse = useGI ? tracedDiffuse.rgb : fallback * (1 - s.metallic) * (1 - f) * LambertDiffuse(s.baseColor) * s.ambientOcclusion;
        float specularAO = saturate(pow(nv + s.ambientOcclusion, exp2(-16 * s.roughness - 1)) - 1 + s.ambientOcclusion);
        result.specular = fallback / NexusPi * f * specularAO * (useGI ? saturate(tracedDiffuse.a - 1) : 1);
        return result;
    }
    TextureCube<float4> irradiance = ResourceDescriptorHeap[e.textures.y];
    TextureCube<float4> prefilter = ResourceDescriptorHeap[e.textures.z];
    Texture2D<float2> lut = ResourceDescriptorHeap[e.textures.w];
    SamplerState sampleState = SamplerDescriptorHeap[e.options.x];
    float nv = saturate(dot(s.normal, v));
    float3 f0 = lerp(0.04.xxx, s.baseColor, s.metallic);
    float2 ab = lut.SampleLevel(sampleState, float2(nv, s.roughness), 0);
    // 積分済みのsplit-sum Fresnel。特定の入射方向に対するBRDF評価とは区別する。
    float3 f = saturate(f0 * ab.x + ab.y);
    if(useGI) result.diffuse = tracedDiffuse.rgb;
    else result.diffuse = irradiance.SampleLevel(sampleState, EnvironmentDirection(s.normal, e), 0).rgb *
        (1 - f) * (1 - s.metallic) * LambertDiffuse(s.baseColor) * s.ambientOcclusion * e.parameters.x;
    float3 r = reflect(-v, s.normal);
    // Filamentの鏡面遮蔽近似。AOは直接光へ掛けない。
    float specularAO = saturate(pow(nv + s.ambientOcclusion, exp2(-16 * s.roughness - 1)) - 1 + s.ambientOcclusion);
    result.specular = prefilter.SampleLevel(sampleState, EnvironmentDirection(r, e), s.roughness * e.parameters.w).rgb * f * specularAO;
    // 反射方向の遮蔽rayで、壁の裏にある環境が鏡面IBLから漏れることも防ぐ。
    // 粗い反射の可視率は単一方向による近似で、近傍形状の鏡像自体は生成しない。
    result.specular *= e.parameters.x * (useGI ? saturate(tracedDiffuse.a - 1) : 1);
    return result;
}
#endif
