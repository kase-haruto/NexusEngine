#ifndef NEXUS_ATMOSPHERE
#define NEXUS_ATMOSPHERE
struct AtmosphereData { float4 sun; float4 scattering; float4 ground; };
// Ground-level single-scattering approximation. Effective slant columns are bounded at
// the horizon; this is deliberately cheaper than planetary LUTs or volumetric integration.
float3 AtmosphereRadiance(float3 direction, AtmosphereData a, bool sunDisk = true) {
    float elevation = a.sun.y;
    float daylight = smoothstep(-.12, .02, elevation);
    float3 betaR = float3(.005802, .013558, .0331) * a.scattering.x;
    float betaM = .003996 * a.scattering.y;
    float3 optical = betaR * 8 + .00444 * a.scattering.y * 1.2;
    float sunMass = rcp(max(elevation, 0) + .025);
    float viewMass = rcp(max(direction.y, 0) + .08);
    float3 sunT = exp(-optical * sunMass);
    float3 viewT = exp(-optical * viewMass);
    float mu = clamp(dot(direction, a.sun.xyz), -1, 1), g = a.scattering.z;
    float phaseR = 3 * (1 + mu * mu) / (16 * 3.14159265);
    float phaseM = (1 - g * g) / (4 * 3.14159265 * pow(max(1 + g * g - 2 * g * mu, .001), 1.5));
    float3 source = (betaR * 8 * phaseR + betaM * 1.2 * phaseM) / max(optical, .000001);
    // Half of the view column approximates extinction between the sun and scattering site.
    float3 color = a.sun.w * daylight * sunT * source * (1 - viewT);
    color += a.scattering.w * float3(.25, .4, 1) * (.4 + .6 * saturate(direction.y));
    if(sunDisk && elevation > -.02) {
        float disk = smoothstep(cos(.0055), cos(.004), mu);
        color += disk * a.sun.w * sunT * smoothstep(-.02, .02, elevation);
    }
    if(direction.y < 0) {
        float3 ground = a.ground.rgb * (a.sun.w * max(elevation, 0) * sunT / 3.14159265 + a.scattering.w);
        color = lerp(color, ground, smoothstep(0, .08, -direction.y));
    }
    return max(color, 0);
}
// Cosine-weighted quadrature in the surface hemisphere; returns E/pi.
float3 AtmosphereDiffuse(float3 normal, AtmosphereData a) {
    float3 tangent = normalize(cross(abs(normal.y) < .99 ? float3(0,1,0) : float3(1,0,0), normal));
    float3 bitangent = cross(normal, tangent);
    float3 sum = 0;
    [unroll] for(uint i = 0; i < 8; ++i) {
        float r = sqrt((i + .5) / 8), phi = i * 2.39996323;
        float3 d = tangent * (r * cos(phi)) + bitangent * (r * sin(phi)) + normal * sqrt(1 - r * r);
        sum += AtmosphereRadiance(d, a, false);
    }
    return sum / 8;
}
#endif
