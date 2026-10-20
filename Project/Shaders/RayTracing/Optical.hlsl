// 交差shaderは材質・幾何属性を収集し、raygenが反射・屈折・GIの経路を追跡する。
// 反復回数に上限を設け、DXRの再帰スタックを増やさない。
#include "../Common/PBR.hlsli"
#include "../Common/BakedLighting.hlsli"
#include "../Common/DDGI.hlsli"
RaytracingAccelerationStructure opticalScene : register(t0);
ByteAddressBuffer surfaceData : register(t1);
// ローカル定数で形状を識別し、GeometryIndex()を持たないDXR Tier1.0にも対応する。
cbuffer GeometryRecord : register(b1) { uint geometryRecordIndex; };
cbuffer OpticalConstants : register(b0)
{
    row_major float4x4 inverseViewProjection;
    float4 lightDirectionIntensity;
    float4 lightColor;
    uint outputIndex;
    uint geometryByteOffset;
    uint maxBounces;
    uint opticalEnabled;
    float4 rayParameters;
    float4 ambientIrradiance;
    EnvironmentData environment;
    uint4 giOptions; // 有効・経路数・最大区間数・出力UAV
    float4 giParameters; // 間接拡散の倍率・境界調整領域
    BakedLightingData bakedLighting;
    DDGIData ddgi;
    uint4 probeTrace;
    uint4 reflectionTargets;
};

struct Surface
{
    float3 normal; float distance;
    float3 color; float reflectivity;
    float3 absorption; float transmission;
    float ior; uint hit; uint front; uint padding;
    float metallic; float roughness;
    float3 emissive; float ao;
    float3 geometricNormal;
}; // BackendのShaderConfigと一致する100byteの交差ペイロード。

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
    const uint geometry = geometryByteOffset + geometryRecordIndex * 144;
    // A primary visibility ray needs no full material evaluation on an ordinary non-metal.
    // Secondary reflections and GI probes still collect all shading attributes.
    if(payload.padding == 0xffffffffu) {
        float2 optics = asfloat(surfaceData.Load2(geometry+48));
        if(all(optics == 0)) {
            float2 mr = asfloat(surfaceData.Load2(geometry+80));
            bool reject = mr.x == 0 || rayParameters.w == 0 || reflectionTargets.z == 0;
            if(!reject) {
                uint4 streams=surfaceData.Load4(geometry), range=surfaceData.Load4(geometry+16);
                ByteAddressBuffer vb=ResourceDescriptorHeap[NonUniformResourceIndex(streams.x)];
                ByteAddressBuffer ib=ResourceDescriptorHeap[NonUniformResourceIndex(streams.y)];
                uint start=range.x+PrimitiveIndex()*3;
                uint3 indices=uint3(ReadIndex(ib,start,streams.w),ReadIndex(ib,start+1,streams.w),ReadIndex(ib,start+2,streams.w))+range.y;
                float3 bary=float3(1-attr.barycentrics.x-attr.barycentrics.y,attr.barycentrics);
                float2 uv=asfloat(vb.Load2(indices.x*streams.z+24))*bary.x +
                    asfloat(vb.Load2(indices.y*streams.z+24))*bary.y + asfloat(vb.Load2(indices.z*streams.z+24))*bary.z;
                uint4 maps=surfaceData.Load4(geometry+96);
                float4 texel=1;
                if(maps.z!=0) {
                    Texture2D<float4> map=ResourceDescriptorHeap[NonUniformResourceIndex(maps.z)];
                    SamplerState state=SamplerDescriptorHeap[NonUniformResourceIndex(maps.w)];
                    texel=map.SampleLevel(state,uv,0);
                }
                mr=MaterialMetallicRoughness(mr,texel);
                reject=mr.x==0 || mr.y>=.5;
            }
            if(reject) {payload=(Surface)0;payload.hit=1;payload.distance=RayTCurrent();return;}
        }
    }
    payload.padding = geometryRecordIndex + 1;
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
    // 法線マップによって法線が三角形の裏へ向くことがある。
    // 幾何法線でレイの始点をずらし、自己交差や光漏れを防ぐ。
    float3 edgeOrigin = asfloat(vertices.Load3(addresses.x));
    float3 edgeA = mul(ObjectToWorld3x4(), float4(asfloat(vertices.Load3(addresses.y)) - edgeOrigin, 0));
    float3 edgeB = mul(ObjectToWorld3x4(), float4(asfloat(vertices.Load3(addresses.z)) - edgeOrigin, 0));
    payload.geometricNormal = SafeNormalize(cross(edgeA, edgeB), payload.normal);
    if(dot(payload.geometricNormal, payload.normal) < 0) payload.geometricNormal = -payload.geometricNormal;
    payload.front = dot(payload.geometricNormal, WorldRayDirection()) < 0;
    float2 uv = asfloat(vertices.Load2(addresses.x + 24)) * weights.x +
        asfloat(vertices.Load2(addresses.y + 24)) * weights.y + asfloat(vertices.Load2(addresses.z + 24)) * weights.z;
    float3 vertexColor = asfloat(vertices.Load3(addresses.x + 32)) * weights.x +
        asfloat(vertices.Load3(addresses.y + 32)) * weights.y + asfloat(vertices.Load3(addresses.z + 32)) * weights.z;
    payload.color = asfloat(surfaceData.Load4(geometry + 32)).rgb * vertexColor;
    if (draw.z != 0)
    {
        Texture2D<float4> texture = ResourceDescriptorHeap[NonUniformResourceIndex(draw.z)];
        SamplerState sampler = SamplerDescriptorHeap[NonUniformResourceIndex(draw.w)];
        // 二次レイには画面微分がないため、明示的なLODで材質をサンプルする。
        payload.color *= texture.SampleLevel(sampler, uv, 0).rgb;
    }
    const float4 optics = asfloat(surfaceData.Load4(geometry + 48));
    payload.reflectivity = optics.x; payload.transmission = optics.y; payload.ior = optics.z;
    payload.absorption = asfloat(surfaceData.Load4(geometry + 64)).rgb;
    float4 pbr = asfloat(surfaceData.Load4(geometry + 80));
    uint4 maps = surfaceData.Load4(geometry + 96);
    float4 mrTexel = 1;
    if(maps.z != 0) {
        Texture2D<float4> map = ResourceDescriptorHeap[NonUniformResourceIndex(maps.z)];
        SamplerState sampleState = SamplerDescriptorHeap[NonUniformResourceIndex(maps.w)];
        float4 mr = map.SampleLevel(sampleState, uv, 0);
        mrTexel = mr;
    }
    float2 metallicRoughness = MaterialMetallicRoughness(pbr.xy, mrTexel);
    payload.metallic = metallicRoughness.x; payload.roughness = metallicRoughness.y;
    float4 emissionAO = asfloat(surfaceData.Load4(geometry + 112));
    uint4 extraMaps = surfaceData.Load4(geometry + 128);
    payload.emissive = emissionAO.rgb; payload.ao = 1;
    if(extraMaps.x != 0) {
        Texture2D<float4> map = ResourceDescriptorHeap[NonUniformResourceIndex(extraMaps.x)];
        SamplerState state = SamplerDescriptorHeap[NonUniformResourceIndex(extraMaps.y)];
        payload.emissive *= map.SampleLevel(state, uv, 0).rgb;
    }
    if(extraMaps.z != 0) {
        Texture2D<float4> map = ResourceDescriptorHeap[NonUniformResourceIndex(extraMaps.z)];
        SamplerState state = SamplerDescriptorHeap[NonUniformResourceIndex(extraMaps.w)];
        payload.ao = MaterialOcclusion(map.SampleLevel(state, uv, 0).r, emissionAO.w);
    }
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
            float3 normalTexel = map.SampleLevel(sampleState, uv, 0).xyz;
            payload.normal = MaterialNormal(normalTexel, pbr.z, n, t, b);
        }
    }
    payload.distance = RayTCurrent(); payload.hit = 1;
}

[shader("miss")]
void OpticalMiss(inout Surface payload) { payload.hit = 0; }

Surface Intersect(float3 origin, float3 direction, bool primaryVisibility = false)
{
    Surface surface = (Surface)0;
    if(primaryVisibility && giOptions.x == 0) surface.padding=0xffffffffu;
    RayDesc ray; ray.Origin = origin; ray.Direction = direction;
    ray.TMin = rayParameters.x; ray.TMax = rayParameters.y;
    TraceRay(opticalScene, RAY_FLAG_FORCE_OPAQUE, 255, 0, 1, 0, ray, surface);
    return surface;
}

float3 Environment(float3 direction)
{
    if(environment.options.y != 0) return SampleEnvironment(direction, environment);
    return ambientIrradiance.rgb / NexusPi;
}
float3 Offset(float3 position, float3 normal, float3 direction)
{
    return position + normal * (dot(normal, direction) > 0 ? rayParameters.x : -rayParameters.x);
}
#include "GlobalIllumination.hlsli"
// プローブraygenも既存のclosesthit・材質マップ・TLASを共有する。
#include "DDGIProbe.hlsli"
struct Path
{
    float3 origin; float3 direction;
    float3 weight; float3 absorption;
    float medium; uint depth;
};

float3 MetalReflectedRadiance(Surface receiver, float3 position, float3 v, uint2 pixel) {
    float3 n = receiver.normal, sum = 0;
    float nv = saturate(dot(n, v));
    float3 f0 = lerp(.04.xxx, receiver.color, receiver.metallic);
    float3 t = normalize(cross(abs(n.z) < .999 ? float3(0,0,1) : float3(1,0,0), n));
    // Deterministic spatial samples; a separate edge-aware pass filters specular radiance.
    const uint samples = receiver.roughness < .08 ? 1u : clamp(uint(rayParameters.z),1u,64u);
    float shift = GIHash(pixel.x + pixel.y * DispatchRaysDimensions().x) * 2.3283064365386963e-10;
    for(uint i = 0; i < samples; ++i) {
        float2 xi = float2((i + .5) / samples, frac(reversebits(i) * 2.3283064365386963e-10 + shift));
        float a = receiver.roughness * receiver.roughness;
        float ct = sqrt((1 - xi.x) / (1 + (a * a - 1) * xi.x));
        float st = sqrt(saturate(1 - ct * ct)), phi = 2 * NexusPi * xi.y;
        float3 h = t * (st * cos(phi)) + cross(n,t) * (st * sin(phi)) + n * ct;
        if(receiver.roughness < .08) h=n; // Stable polished reflection; avoid per-pixel random sparkle.
        float3 l = reflect(-v,h);
        float nl = saturate(dot(n,l)), nh = saturate(dot(n,h)), vh = saturate(dot(v,h));
        if(nl <= 0 || dot(receiver.geometricNormal,l) <= 0 || vh <= 0) continue;
        float3 origin = Offset(position,receiver.geometricNormal,l);
        Surface hit = Intersect(origin,l);
        float3 incoming = Environment(l);
        if(hit.hit != 0) {
            float3 p = origin + l * hit.distance;
            float3 normal = hit.front != 0 ? hit.normal : -hit.normal;
            SurfaceData s = MakeSurface(hit.color,hit.metallic,hit.roughness,normal,hit.ao,hit.emissive);
            float3 light = SafeNormalize(lightDirectionIntensity.xyz,float3(0,1,0));
            float visible = !Occluded(Offset(p,hit.geometricNormal,light),light);
            float4 gi = EvaluateBakedLighting(s,-l,p,0,bakedLighting,environment);
            float4 dynamicGI = EvaluateDDGILighting(s,-l,p,ddgi,environment);
            if(dynamicGI.a > .5) gi = dynamicGI;
            EnvironmentData secondaryEnvironment = environment;
            secondaryEnvironment.options.z = 0; // Primary diagnostics must not change reflected incident radiance.
            incoming = EvaluatePBRLighting(s,-l,light,lightColor.rgb * max(lightDirectionIntensity.w,0),visible,
                secondaryEnvironment,ambientIrradiance.rgb,gi);
        }
        // GGX BRDF * NdotL / (D * NdotH / (4 * VdotH)).
        float weight = 4 * VisibilitySmith(nv,nl,receiver.roughness) * nl * vh / max(nh,1e-6);
        sum += incoming * FresnelSchlick(vh,f0) * weight;
    }
    return sum / samples * receiver.ao;
}

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
    RWTexture2D<float4> diffuseOutput = ResourceDescriptorHeap[giOptions.w];
    RWTexture2D<float4> reflectionOutput = ResourceDescriptorHeap[reflectionTargets.x];
    RWTexture2D<float4> guideOutput = ResourceDescriptorHeap[reflectionTargets.y];
    if(reflectionTargets.z != 0) {
        reflectionOutput[pixel] = 0;
        guideOutput[pixel] = 0;
    }
    const Surface primary = Intersect(nearPosition, direction,true);
    float4 primaryGI = 0;
    const bool evaluateGI = giOptions.x != 0 && (environment.options.z == 0 || environment.options.z == 2 || environment.options.z == 11);
    if(primary.hit != 0 && evaluateGI) {
        const bool pureOptical = opticalEnabled != 0 && (primary.reflectivity == 1 || primary.transmission == 1);
        primaryGI = pureOptical ? float4(0,0,0,1) : EvaluateGI(primary, nearPosition + direction * primary.distance, -direction, pixel);
    }
    if(giOptions.x != 0) diffuseOutput[pixel] = float4(clamp(primaryGI.rgb, 0, 65504), primaryGI.a);
    if(primary.hit != 0 && opticalEnabled != 0 && reflectionTargets.z != 0 && rayParameters.w != 0 && (environment.options.z == 0 || environment.options.z == 2) && primary.metallic > 0 && primary.roughness < .5 &&
       primary.reflectivity == 0 && primary.transmission == 0 && primary.front != 0) {
        float3 p = nearPosition + direction * primary.distance;
        SurfaceData s = MakeSurface(primary.color,primary.metallic,primary.roughness,primary.normal,primary.ao,primary.emissive);
        float3 light = SafeNormalize(lightDirectionIntensity.xyz,float3(0,1,0));
        float visible = !Occluded(Offset(p,primary.geometricNormal,light),light);
        float4 gi = EvaluateBakedLighting(s,-direction,p,0,bakedLighting,environment);
        float4 dynamicGI = EvaluateDDGILighting(s,-direction,p,ddgi,environment);
        if(dynamicGI.a > .5) gi = dynamicGI;
        if(evaluateGI) gi = primaryGI;
        IndirectLighting indirect = EvaluateIndirectLighting(s,-direction,environment,ambientIrradiance.rgb,gi);
        float localWeight = 1 - smoothstep(.2,.5,primary.roughness);
        float3 reflected = MetalReflectedRadiance(primary,p,-direction,pixel) * localWeight;
        reflectionOutput[pixel] = float4(clamp(reflected,0,65504),primary.distance);
        guideOutput[pixel] = float4(DDGIOctEncode(primary.normal),primary.roughness,float(primary.padding));
        float3 radiance = indirect.diffuse + indirect.specular * (1-localWeight);
        if(environment.options.z == 0) radiance += s.emissive +
            EvaluateDirectLighting(s,-direction,light,lightColor.rgb * max(lightDirectionIntensity.w,0),visible,environment.options.w != 0);
        output[pixel] = float4(clamp(radiance,0,65504),1);
        return;
    }
    if (primary.hit == 0 || opticalEnabled == 0 || environment.options.z != 0 || (primary.reflectivity == 0 && primary.transmission == 0))
    {
        output[pixel] = 0; // 通常pixelの最終合成はRasterの照明処理が担当する。
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
        float visibility = lightDirectionIntensity.w > 0 && Occluded(Offset(position, hit.geometricNormal, l), l) ? 0 : 1;
        SurfaceData surface = MakeSurface(hit.color, hit.metallic, hit.roughness, normal, hit.ao, hit.emissive);
        // 通常PBRローブと理想反射・透過の重みを分配し、
        // 同じエネルギーをIBLとDXRで二重加算しない。
        surface.emissive = 0;
        float4 secondaryGI = EvaluateBakedLighting(surface,-path.direction,position,0,bakedLighting,environment);
        float4 dynamicGI=EvaluateDDGILighting(surface,-path.direction,position,ddgi,environment);
        if(dynamicGI.a>.5) secondaryGI=dynamicGI;
        if(evaluateGI && diffuse > 0) secondaryGI = path.depth == 0 ? primaryGI :
            EvaluateGI(hit, position, -path.direction, pixel + uint2(path.depth * 17, 31));
        float3 lighting = EvaluatePBRLighting(surface, -path.direction, l,
            lightColor.rgb * max(lightDirectionIntensity.w, 0), visibility, environment, ambientIrradiance.rgb, secondaryGI);
        radiance += weight * (diffuse * lighting + hit.emissive);
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
            // 入射と出射を追跡し、実際のガラス厚でBeer吸収を評価する。
            child.direction = normalize(refracted); child.origin = Offset(position, hit.normal, child.direction);
            child.weight = weight * transmission; child.medium = nextMedium;
            child.absorption = hit.front != 0 ? hit.absorption : 0;
            paths[count++] = child;
        }
    }
    // RGBA16_FLOATの範囲に制限し、無限大がポスト処理へ伝播することを防ぐ。
    output[pixel] = float4(clamp(radiance, 0, 65504), 1);
}
