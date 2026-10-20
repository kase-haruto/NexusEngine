#ifndef NEXUS_BRDF
#define NEXUS_BRDF
static const float NexusPi = 3.14159265359;
float3 SafeNormalize(float3 value, float3 fallback) {
    float len2 = dot(value, value); return len2 > 1e-12 ? value * rsqrt(len2) : fallback;
}
// BRDF terms only: no NdotL, radiance or shadow visibility.
float DistributionGGX(float nh, float roughness) {
    float alpha = max(roughness * roughness, 0.0025), a2 = alpha * alpha;
    float d = (1 - nh * nh) + nh * nh * a2;
    return a2 / (NexusPi * d * d);
}
// Height-correlated Smith G / (4 NdotV NdotL).
float VisibilitySmith(float nv, float nl, float roughness) {
    float a = max(roughness * roughness, 0.0025), a2 = a * a;
    return 0.5 / max(nl * sqrt(nv * nv * (1 - a2) + a2) + nv * sqrt(nl * nl * (1 - a2) + a2), 1e-8);
}
float3 FresnelSchlick(float vh, float3 f0) { return f0 + (1 - f0) * pow(1 - saturate(vh), 5); }
float3 LambertDiffuse(float3 color) { return color / NexusPi; }
float3 SpecularBRDF(float nh, float nv, float nl, float roughness, float3 fresnel) {
    return DistributionGGX(nh, roughness) * VisibilitySmith(nv, nl, roughness) * fresnel;
}
#endif
