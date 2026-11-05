#ifndef NEXUS_SURFACE_DATA
#define NEXUS_SURFACE_DATA
// Material解決後の線形値。テクスチャ参照やstageを含めない。
struct SurfaceData {
    float3 baseColor; float metallic; float roughness; float3 normal;
    float ambientOcclusion; float3 emissive;
};
SurfaceData MakeSurface(float3 color, float metallic, float roughness, float3 normal, float ao, float3 emissive) {
    SurfaceData s;
    s.baseColor = saturate(color); s.metallic = saturate(metallic);
    // Preserve polished materials for IBL, ray reflections and material diagnostics.
    // The analytic BRDF applies its numerical floor locally.
    s.roughness = saturate(roughness); s.normal = normal;
    s.ambientOcclusion = saturate(ao); s.emissive = max(emissive, 0);
    return s;
}
#endif
