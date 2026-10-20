#ifndef NEXUS_PBR_HLSLI
#define NEXUS_PBR_HLSLI
#include "DirectLighting.hlsli"
#include "IndirectLighting.hlsli"
#include "Material.hlsli"
// 描画方式で共通の照明合成・診断表示。出力は線形HDR。
float3 EvaluatePBRLighting(SurfaceData s, float3 v, float3 l, float3 incident, float visibility,
                          EnvironmentData e, float3 fallback, float4 diffuseGI = 0) {
    // 一様な表示切替で、材質検査時には不要な照明textureの参照を省く。
    switch(e.options.z) {
        case 5: return s.baseColor;
        case 6: return s.metallic.xxx;
        case 7: return s.roughness.xxx;
        case 8: return s.normal * 0.5 + 0.5;
        case 9: return s.ambientOcclusion.xxx;
        case 10: return s.emissive;
        case 11: return diffuseGI.a > 0.5 ? diffuseGI.rgb : 0;
    }
    float3 direct = 0;
    if(e.options.z <= 1) direct = EvaluateDirectLighting(s, v, l, incident, visibility, e.options.w != 0);
    if(e.options.z == 1) return direct;
    IndirectLighting indirect = EvaluateIndirectLighting(s, v, e, fallback, diffuseGI);
    if(e.options.z == 2) return indirect.diffuse + indirect.specular;
    if(e.options.z == 3) return indirect.diffuse;
    if(e.options.z == 4) return indirect.specular;
    return direct + indirect.diffuse + indirect.specular + s.emissive;
}
#endif
