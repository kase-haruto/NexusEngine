#ifndef NEXUS_DIRECT_LIGHTING
#define NEXUS_DIRECT_LIGHTING
#include "SurfaceData.hlsli"
#include "BRDF.hlsli"
float3 EvaluateDirectLighting(SurfaceData s, float3 v, float3 l, float3 radiance, float visibility, bool legacy) {
    float nl = saturate(dot(s.normal, l)), nv = saturate(dot(s.normal, v));
    if(nv <= 0) return 0;
    float3 h = SafeNormalize(v + l, s.normal);
    float3 f = FresnelSchlick(dot(v, h), lerp(0.04.xxx, s.baseColor, s.metallic));
    float dw = legacy ? pow(saturate(dot(s.normal, l) * 0.5 + 0.5), 2) : nl;
    float3 diffuse = (1 - f) * (1 - s.metallic) * LambertDiffuse(s.baseColor) * dw;
    float3 specular = SpecularBRDF(saturate(dot(s.normal, h)), nv, nl, s.roughness, f) * nl;
    return (diffuse + specular) * radiance * saturate(visibility);
}
#endif
