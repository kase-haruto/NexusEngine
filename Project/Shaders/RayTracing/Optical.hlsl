// Hit exports only gather surface attributes. Bounded Raygen paths handle both
// reflection and refraction without recursively growing the DXR pipeline stack.
#include "../Common/PBR.hlsli"
RaytracingAccelerationStructure opticalScene : register(t0);
ByteAddressBuffer surfaceData : register(t1);
// Local ShaderTable constants work on Tier1.0 too; GeometryIndex() requires Tier1.1.
cbuffer GeometryRecord : register(b1) { uint geometryRecordIndex; };
cbuffer OpticalConstants : register(b0)
{
    row_major float4x4 inverseViewProjection;
    float4 lightDirectionIntensity;
    float4 lightColor;
    uint outputIndex;
    uint geometryByteOffset;
    uint maxBounces;
    uint opticalPadding;
    float4 rayParameters;
    float4 ambientIrradiance;
};

struct Surface
{
    float3 normal; float distance;
    float3 color; float reflectivity;
    float3 absorption; float transmission;
    float ior; uint hit; uint front; uint padding;
    float metallic; float roughness;
}; // 72byte payload, matched by Backend ShaderConfig.

uint ReadIndex(ByteAddressBuffer buffer, uint index, uint wide)
{
    if (wide != 0) return buffer.Load(index * 4);
    const uint offset = index * 2;
    return (buffer.Load(offset & ~3u) >> ((offset & 2u) * 8)) & 65535u;
}

[shader("closesthit")]
void OpticalHit(inout Surface payload, BuiltInTriangleIntersectionAttributes attr)
{
    const uint instance = InstanceID() * 80;
    const uint geometry = geometryByteOffset + geometryRecordIndex * 112;
    const uint4 buffers = surfaceData.Load4(geometry);
    const uint4 draw = surfaceData.Load4(geometry + 16);
    ByteAddressBuffer vertices = ResourceDescriptorHeap[NonUniformResourceIndex(buffers.x)];
    ByteAddressBuffer indices = ResourceDescriptorHeap[NonUniformResourceIndex(buffers.y)];
    const uint first = draw.x + PrimitiveIndex() * 3;
    const uint3 vertex = uint3(ReadIndex(indices, first, buffers.w), ReadIndex(indices, first + 1, buffers.w),
        ReadIndex(indices, first + 2, buffers.w)) + draw.y;
    const uint3 addresses = vertex * buffers.z;
    const float3 weights = float3(1 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics);
    float3 normal = asfloat(vertices.Load3(addresses.x + 12)) * weights.x +
        asfloat(vertices.Load3(addresses.y + 12)) * weights.y + asfloat(vertices.Load3(addresses.z + 12)) * weights.z;
    const row_major float4x4 transform = float4x4(asfloat(surfaceData.Load4(instance)),
        asfloat(surfaceData.Load4(instance + 16)), asfloat(surfaceData.Load4(instance + 32)), asfloat(surfaceData.Load4(instance + 48)));
    payload.normal = normalize(mul(float4(normal, 0), transform).xyz);
    payload.front = dot(payload.normal, WorldRayDirection()) < 0;
    float2 uv = asfloat(vertices.Load2(addresses.x + 24)) * weights.x +
        asfloat(vertices.Load2(addresses.y + 24)) * weights.y + asfloat(vertices.Load2(addresses.z + 24)) * weights.z;
    float3 vertexColor = asfloat(vertices.Load3(addresses.x + 32)) * weights.x +
        asfloat(vertices.Load3(addresses.y + 32)) * weights.y + asfloat(vertices.Load3(addresses.z + 32)) * weights.z;
    payload.color = asfloat(surfaceData.Load4(geometry + 32)).rgb * vertexColor;
    if (draw.z != 0)
    {
        Texture2D<float4> texture = ResourceDescriptorHeap[NonUniformResourceIndex(draw.z)];
        SamplerState sampler = SamplerDescriptorHeap[NonUniformResourceIndex(draw.w)];
        // Secondary rays have no raster derivatives. Explicit LOD keeps sampling legal.
        payload.color *= texture.SampleLevel(sampler, uv, 0).rgb;
    }
    const float4 optics = asfloat(surfaceData.Load4(geometry + 48));
    payload.reflectivity = optics.x; payload.transmission = optics.y; payload.ior = optics.z;
    payload.absorption = asfloat(surfaceData.Load4(geometry + 64)).rgb;
    float4 pbr = asfloat(surfaceData.Load4(geometry + 80));
    uint4 maps = surfaceData.Load4(geometry + 96);
    payload.metallic = pbr.x; payload.roughness = pbr.y;
    if(maps.z != 0) {
        Texture2D<float4> map = ResourceDescriptorHeap[NonUniformResourceIndex(maps.z)];
        SamplerState sampleState = SamplerDescriptorHeap[NonUniformResourceIndex(maps.w)];
        float4 mr = map.SampleLevel(sampleState, uv, 0);
        payload.metallic *= mr.b; payload.roughness *= mr.g;
    }
    payload.metallic = saturate(payload.metallic); payload.roughness = clamp(payload.roughness, 0.05, 1);
    if(maps.x != 0) {
        float4 tangent = asfloat(vertices.Load4(addresses.x + 80)) * weights.x +
            asfloat(vertices.Load4(addresses.y + 80)) * weights.y + asfloat(vertices.Load4(addresses.z + 80)) * weights.z;
        float3 n = payload.normal, t, b;
        if(abs(tangent.w) > 0.5 && dot(tangent.xyz, tangent.xyz) > 1e-12) {
            t = mul(ObjectToWorld3x4(), float4(tangent.xyz, 0));
            t = SafeNormalize(t - n * dot(n, t), float3(1, 0, 0));
            b = cross(n, t) * tangent.w * (determinant((float3x3)ObjectToWorld3x4()) < 0 ? -1 : 1);
        } else {
            float3 p0 = asfloat(vertices.Load3(addresses.x));
            float3 e1 = asfloat(vertices.Load3(addresses.y)) - p0, e2 = asfloat(vertices.Load3(addresses.z)) - p0;
            float2 uv0 = asfloat(vertices.Load2(addresses.x + 24));
            float2 uv1 = asfloat(vertices.Load2(addresses.y + 24)) - uv0, uv2 = asfloat(vertices.Load2(addresses.z + 24)) - uv0;
            float det = uv1.x * uv2.y - uv1.y * uv2.x;
            t = abs(det) > 1e-10 ? mul(ObjectToWorld3x4(), float4((e1 * uv2.y - e2 * uv1.y) / det, 0)) : 0;
            b = abs(det) > 1e-10 ? mul(ObjectToWorld3x4(), float4((e2 * uv1.x - e1 * uv2.x) / det, 0)) : 0;
            t = SafeNormalize(t - n * dot(n, t), float3(1, 0, 0));
            b = cross(n, t) * (dot(cross(n, t), b) < 0 ? -1 : 1);
            if(abs(det) < 1e-10) b = 0;
        }
        if(dot(b, b) > 1e-12) {
            Texture2D<float4> map = ResourceDescriptorHeap[NonUniformResourceIndex(maps.x)];
            SamplerState sampleState = SamplerDescriptorHeap[NonUniformResourceIndex(maps.y)];
            float3 mapped = map.SampleLevel(sampleState, uv, 0).xyz * 2 - 1; mapped.xy *= pbr.z;
            payload.normal = SafeNormalize(t * mapped.x + b * mapped.y + n * mapped.z, n);
        }
    }
    payload.distance = RayTCurrent(); payload.hit = 1;
}

[shader("miss")]
void OpticalMiss(inout Surface payload) { payload.hit = 0; }

Surface Intersect(float3 origin, float3 direction)
{
    Surface surface = (Surface)0;
    RayDesc ray; ray.Origin = origin; ray.Direction = direction;
    ray.TMin = rayParameters.x; ray.TMax = rayParameters.y;
    TraceRay(opticalScene, RAY_FLAG_FORCE_OPAQUE, 255, 0, 1, 0, ray, surface);
    return surface;
}

float3 Environment(float3 direction)
{
    return lerp(float3(0.08, 0.11, 0.16), float3(0.45, 0.65, 0.95), saturate(direction.y * 0.5 + 0.5));
}
float3 Offset(float3 position, float3 normal, float3 direction)
{
    return position + normal * (dot(normal, direction) > 0 ? rayParameters.x : -rayParameters.x);
}
struct Path
{
    float3 origin; float3 direction;
    float3 weight; float3 absorption;
    float medium; uint depth;
};

[shader("raygeneration")]
void OpticalRaygen()
{
    const uint2 pixel = DispatchRaysIndex().xy;
    const float2 uv = (float2(pixel) + 0.5) / float2(DispatchRaysDimensions().xy);
    const float2 ndc = float2(uv.x * 2 - 1, 1 - uv.y * 2);
    const float4 nearH = mul(float4(ndc, 0, 1), inverseViewProjection);
    const float4 farH = mul(float4(ndc, 1, 1), inverseViewProjection);
    const float3 nearPosition = nearH.xyz / nearH.w;
    const float3 direction = normalize(farH.xyz / farH.w - nearPosition);
    RWTexture2D<float4> output = ResourceDescriptorHeap[outputIndex];
    const Surface primary = Intersect(nearPosition, direction);
    if (primary.hit == 0 || (primary.reflectivity == 0 && primary.transmission == 0))
    {
        output[pixel] = 0; // Opaque Forward Lighting retains ownership of ordinary pixels.
        return;
    }

    Path paths[16]; uint count = 1;
    paths[0].origin = nearPosition; paths[0].direction = direction; paths[0].weight = 1;
    paths[0].absorption = primary.front != 0 ? 0 : primary.absorption;
    paths[0].medium = primary.front != 0 ? 1 : primary.ior; paths[0].depth = 0;
    float3 radiance = 0;
    while (count > 0)
    {
        const Path path = paths[--count];
        Surface hit = primary;
        if (path.depth != 0) hit = Intersect(path.origin, path.direction);
        if (hit.hit == 0) { radiance += path.weight * Environment(path.direction); continue; }
        const float3 weight = path.weight * exp(-path.absorption * hit.distance);
        const float3 position = path.origin + path.direction * hit.distance;
        const float3 normal = hit.front != 0 ? hit.normal : -hit.normal;
        const float diffuse = (1 - hit.reflectivity) * (1 - hit.transmission);
        float3 l = SafeNormalize(lightDirectionIntensity.xyz, float3(0, 1, 0));
        Surface blocker = Intersect(Offset(position, hit.normal, l), l);
        float visibility = blocker.hit == 0 ? 1 : 0;
        float3 lighting = EvaluatePBR(saturate(hit.color), hit.metallic, hit.roughness, normal, -path.direction, l) *
            lightColor.rgb * max(lightDirectionIntensity.w, 0) * visibility;
        lighting += ambientIrradiance.rgb * hit.color * (1 - hit.metallic) * 0.96 / NexusPi;
        radiance += weight * diffuse * lighting;
        if (path.depth + 1 >= maxBounces) continue;
        const float nextMedium = hit.front != 0 ? hit.ior : 1;
        const float cosine = saturate(dot(-path.direction, normal));
        const float f0 = pow((path.medium - nextMedium) / (path.medium + nextMedium), 2);
        const float fresnel = f0 + (1 - f0) * pow(1 - cosine, 5);
        const float3 refracted = refract(path.direction, normal, path.medium / nextMedium);
        const bool totalInternalReflection = dot(refracted, refracted) < 1e-8;
        float reflection = hit.reflectivity;
        float transmission = 0;
        if (hit.transmission > 0)
        {
            reflection += (1 - hit.reflectivity) * hit.transmission * (totalInternalReflection ? 1 : fresnel);
            transmission = (1 - hit.reflectivity) * hit.transmission * (totalInternalReflection ? 0 : 1 - fresnel);
        }
        Path child; child.depth = path.depth + 1;
        if (reflection > 0 && max(weight.r, max(weight.g, weight.b)) * reflection > 0.002 && count < 16)
        {
            child.direction = reflect(path.direction, normal);
            child.origin = Offset(position, hit.normal, child.direction);
            child.weight = weight * reflection * (hit.transmission > 0 ? 1 : hit.color);
            child.medium = path.medium; child.absorption = path.absorption;
            paths[count++] = child;
        }
        if (transmission > 0 && max(weight.r, max(weight.g, weight.b)) * transmission > 0.002 && count < 16)
        {
            // Enter/exit are both traced. Beer attenuation therefore uses actual glass thickness.
            child.direction = normalize(refracted); child.origin = Offset(position, hit.normal, child.direction);
            child.weight = weight * transmission; child.medium = nextMedium;
            child.absorption = hit.front != 0 ? hit.absorption : 0;
            paths[count++] = child;
        }
    }
    // The optical UAV uses RGBA16_FLOAT; infinity would contaminate post-processing.
    output[pixel] = float4(clamp(radiance, 0, 65504), 1);
}
