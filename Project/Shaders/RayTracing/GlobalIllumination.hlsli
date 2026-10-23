#ifndef NEXUS_GLOBAL_ILLUMINATION
#define NEXUS_GLOBAL_ILLUMINATION
// 以下はraygenから呼び出し、closesthitは属性収集だけを担当する。
// 拡散・反射経路を複数区間追跡しても、DXRの再帰深度は1に保つ。
bool Occluded(float3 origin, float3 direction) {
    Surface blocker = (Surface)0; blocker.hit = 1;
    RayDesc ray; ray.Origin = origin; ray.Direction = direction;
    // 始点は既に幾何法線で移動済み。TMinにも同じbiasを掛けると、
    // 角付近で隣接する壁を飛び越えて光が漏れるため二重の除外を避ける。
    ray.TMin = 0; ray.TMax = rayParameters.y;
    TraceRay(opticalScene, RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
        255, 0, 1, 0, ray, blocker);
    return blocker.hit != 0;
}
Surface GIIntersect(float3 origin, float3 direction) {
    Surface hit = (Surface)0;
    RayDesc ray; ray.Origin = origin; ray.Direction = direction;
    ray.TMin = 0; ray.TMax = rayParameters.y;
    TraceRay(opticalScene,RAY_FLAG_FORCE_OPAQUE,255,0,1,0,ray,hit);
    return hit;
}
uint GIHash(uint value) {
    value ^= value >> 16; value *= 0x7feb352du;
    value ^= value >> 15; value *= 0x846ca68bu; return value ^ (value >> 16);
}
float2 GISample(uint sampleIndex, uint seed, uint segment) {
    // Cranley-Patterson回転で経路の次元間相関を抑える。フレーム番号は含めず、
    // 履歴や再投影なしでも静止シーンのサンプルを時間方向に固定する。
    float2 shift = float2(GIHash(seed + segment * 2), GIHash(seed + segment * 2 + 1)) * 2.3283064365386963e-10;
    return frac(float2((sampleIndex + 0.5) / giOptions.y, reversebits(sampleIndex) * 2.3283064365386963e-10) + shift);
}
float3 GICosineDirection(float2 xi, float3 n) {
    float radius = sqrt(xi.x), phi = 2 * NexusPi * xi.y;
    float3 t = normalize(cross(abs(n.z) < 0.999 ? float3(0,0,1) : float3(1,0,0), n));
    return t * (radius * cos(phi)) + cross(n, t) * (radius * sin(phi)) + n * sqrt(1 - xi.x);
}
float3 GIDiffuseWeight(Surface hit, float3 n, float3 v, float3 l) {
    // Lambertの1/πとcosine重点サンプリングのPDF（NdotL/π）が相殺する。
    // 戻り値は経路の重みBRDF*NdotL/PDFで、直接光に使うBRDFそのものではない。
    float3 h = SafeNormalize(v + l, n);
    float3 f = FresnelSchlick(saturate(dot(v,h)), lerp(0.04.xxx, saturate(hit.color), hit.metallic));
    return (1 - f) * (1 - hit.metallic) * saturate(hit.color) * hit.ao *
        (1 - hit.reflectivity) * (1 - hit.transmission);
}
float3 EvaluateDiffuseGI(Surface receiver, float3 position, float3 v, uint2 pixel) {
    if(receiver.metallic >= 1 || receiver.ao <= 0 || max(receiver.color.r,max(receiver.color.g,receiver.color.b)) <= 0) return 0;
    float3 n = receiver.front != 0 ? receiver.normal : -receiver.normal;
    float3 ng = receiver.front != 0 ? receiver.geometricNormal : -receiver.geometricNormal;
    uint seed = GIHash(pixel.x + pixel.y * 65537u);
    float3 sum = 0;
    for(uint sampleIndex=0; sampleIndex<giOptions.y; ++sampleIndex) {
        float3 direction = GICosineDirection(GISample(sampleIndex,seed,0),n);
        if(dot(ng,direction) <= 0) continue;
        // 鏡・透過のローブ配分は呼び出し側で一度だけ掛ける。
        Surface ordinaryReceiver = receiver; ordinaryReceiver.reflectivity = 0; ordinaryReceiver.transmission = 0;
        float3 throughput = GIDiffuseWeight(ordinaryReceiver,n,v,direction);
        float3 origin = Offset(position,ng,direction);
        for(uint segment=0; segment<giOptions.z; ++segment) {
            Surface hit = GIIntersect(origin,direction);
            if(hit.hit == 0) {
                // 一定環境照度は遠方境界の一様な放射へ変換して扱う。
                // 閉じた形状内の交差点へ未遮蔽のambientやIBLを直接加算しない。
                float3 incoming = environment.options.y != 0 ? SampleEnvironment(direction,environment) : ambientIrradiance.rgb / NexusPi;
                sum += throughput * incoming; break;
            }
            float3 p = origin + direction * hit.distance;
            float3 normal = hit.front != 0 ? hit.normal : -hit.normal;
            float3 geometricNormal = hit.front != 0 ? hit.geometricNormal : -hit.geometricNormal;
            SurfaceData s = MakeSurface(hit.color,hit.metallic,hit.roughness,normal,hit.ao,hit.emissive);
            float3 incoming = s.emissive;
            if(lightDirectionIntensity.w > 0) {
                float3 l = SafeNormalize(lightDirectionIntensity.xyz,float3(0,1,0));
                float visibility = dot(geometricNormal,l) > 0 && !Occluded(Offset(p,geometricNormal,l),l) ? 1 : 0;
                incoming += EvaluateDirectLighting(s,-direction,l,lightColor.rgb * lightDirectionIntensity.w,visibility,false) *
                    (1 - hit.reflectivity) * (1 - hit.transmission);
            }
            sum += throughput * incoming;
            if(segment + 1 >= giOptions.z) break;
            float3 next = GICosineDirection(GISample(sampleIndex,seed,segment+1),normal);
            if(dot(geometricNormal,next) <= 0) break;
            throughput *= GIDiffuseWeight(hit,normal,-direction,next);
            if(max(throughput.r,max(throughput.g,throughput.b)) < 1e-5) break;
            origin = Offset(p,geometricNormal,next); direction = next;
        }
    }
    return sum * (giParameters.x / giOptions.y);
}
float4 EvaluateGI(Surface receiver, float3 position, float3 v, uint2 pixel) {
    float3 diffuse = EvaluateDiffuseGI(receiver,position,v,pixel);
    float visibility = 1;
    if(environment.options.y != 0 || any(ambientIrradiance.rgb > 0)) {
        float3 n = receiver.front != 0 ? receiver.normal : -receiver.normal;
        float3 ng = receiver.front != 0 ? receiver.geometricNormal : -receiver.geometricNormal;
        float3 r = reflect(-v,n);
        visibility = dot(ng,r) > 0 && !Occluded(Offset(position,ng,r),r) ? 1 : 0;
    }
    // 同じ出力のalphaへ有効フラグと鏡面環境の可視率を詰め、texture追加を避ける。
    return float4(diffuse,1 + visibility);
}
#endif
