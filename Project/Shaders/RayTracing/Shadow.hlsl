#include "../Shadow/ShadowConstants.hlsli"

RaytracingAccelerationStructure sceneAcceleration : register(t0);
struct ShadowPayload { uint visible; };

// Stateless spatial samples keep the hard-shadow baseline deterministic. Temporal accumulation
// and denoising are separate future passes; no frame-history resource is implicitly required.
uint ShadowHash(uint value)
{
    value ^= value >> 16; value *= 0x7feb352d;
    value ^= value >> 15; value *= 0x846ca68b;
    return value ^ (value >> 16);
}
float3 ShadowDirection(uint2 pixel, uint sampleIndex)
{
    const float3 direction = lightDirectionAngularRadius.xyz;
    const float radius = lightDirectionAngularRadius.w;
    if (radius == 0.0f) return direction;
    const uint seed = ShadowHash(pixel.x + pixel.y * shadowDimensions.x + sampleIndex * 0x9e3779b9);
    const float u = float(seed & 0xffff) / 65536.0f;
    const float v = float(seed >> 16) / 65536.0f;
    const float3 axis = abs(direction.y) < 0.99f ? float3(0, 1, 0) : float3(1, 0, 0);
    const float3 tangent = normalize(cross(axis, direction));
    const float3 bitangent = cross(direction, tangent);
    const float cosine = lerp(1.0f, cos(radius), u);
    const float sine = sqrt(max(0.0f, 1.0f - cosine * cosine));
    const float angle = v * 6.28318530718f;
    return direction * cosine + (tangent * cos(angle) + bitangent * sin(angle)) * sine;
}

[shader("raygeneration")]
void ShadowRaygen()
{
    const uint2 pixel = DispatchRaysIndex().xy;
    Texture2D<float> depthTexture = ResourceDescriptorHeap[shadowIndices.x];
    RWTexture2D<float> mask = ResourceDescriptorHeap[shadowIndices.z];
    const float depth = depthTexture.Load(int3(pixel, 0));
    if (depth >= 1.0f) { mask[pixel] = 1.0f; return; }
    const float3 position = ReconstructShadowSurface(pixel, depth);
    float visibility = 0.0f;
    for (uint sampleIndex = 0; sampleIndex < shadowIndices.w; ++sampleIndex)
    {
        const float3 direction = ShadowDirection(pixel, sampleIndex);
        RayDesc ray;
        // Bias is in world units and uses the Light direction, independent of material normals.
        // TMin adds an explicit configurable exclusion interval rather than a hidden epsilon.
        ray.Origin = position + direction * shadowBiases.x;
        ray.Direction = direction;
        ray.TMin = shadowBiases.y; ray.TMax = shadowBiases.z;
        ShadowPayload payload;
        payload.visible = 0;
        TraceRay(sceneAcceleration, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
            RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xff, 0, 0, 0, ray, payload);
        visibility += float(payload.visible);
    }
    mask[pixel] = visibility / float(shadowIndices.w);
}
[shader("miss")]
void ShadowMiss(inout ShadowPayload payload) { payload.visible = 1; }
[shader("closesthit")]
void ShadowClosestHit(inout ShadowPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    payload.visible = 0;
}
