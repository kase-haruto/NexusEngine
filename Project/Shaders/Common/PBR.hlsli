#ifndef NEXUS_PBR_HLSLI
#define NEXUS_PBR_HLSLI
static const float NexusPi = 3.14159265359;
float3 SafeNormalize(float3 value, float3 fallback) {
    float lengthSquared = dot(value, value);
    return lengthSquared > 1e-12 ? value * rsqrt(lengthSquared) : fallback;
}
// glTF metallic/roughness: GGX distribution, height-correlated Smith visibility,
// Schlick Fresnel with artistic Half-Lambert diffuse; specular retains N dot L.
float3 EvaluatePBR(float3 baseColor, float metallic, float roughness, float3 n, float3 v, float3 l) {
    float signedNl = dot(n, l);
    float nl = saturate(signedNl), nv = saturate(dot(n, v));
    if(nv <= 0) return 0;
    float halfLambert = saturate(signedNl * 0.5 + 0.5);
    halfLambert *= halfLambert;
    float3 h = SafeNormalize(v + l, n);
    float nh = saturate(dot(n, h)), vh = saturate(dot(v, h));
    float alpha = max(roughness * roughness, 0.0025);
    float a2 = alpha * alpha;
    float denominator = (1 - nh * nh) + nh * nh * a2;
    float distribution = a2 / (NexusPi * denominator * denominator);
    float visibility = 0.5 / max(nl * sqrt(nv * nv * (1 - a2) + a2) +
        nv * sqrt(nl * nl * (1 - a2) + a2), 1e-8);
    float3 f0 = lerp(0.04.xxx, baseColor, metallic);
    float3 fresnel = f0 + (1 - f0) * pow(1 - vh, 5);
    float3 diffuse = (1 - fresnel) * (1 - metallic) * baseColor / NexusPi;
    return diffuse * halfLambert + distribution * visibility * fresnel * nl;
}
#endif
