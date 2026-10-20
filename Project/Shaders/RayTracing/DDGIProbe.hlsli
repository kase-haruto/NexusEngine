#ifndef NEXUS_DDGI_PROBE
#define NEXUS_DDGI_PROBE
[shader("raygeneration")]
void DDGIProbeRaygen() {
    uint rayIndex=DispatchRaysIndex().x,probe=DispatchRaysIndex().y;
    float3 origin=DDGIPosition(probe,ddgi),direction=DDGIRayDirection(rayIndex,ddgi.counts.w,probeTrace.y);
    // 格子点が面上にある場合も交差を検出し、分類と再配置へ渡す。TMinのbiasで面を飛び越さない。
    RayDesc ray;ray.Origin=origin;ray.Direction=direction;ray.TMin=0;ray.TMax=ddgi.sampling.z;
    Surface hit=(Surface)0;
    TraceRay(opticalScene,RAY_FLAG_FORCE_OPAQUE,255,0,1,0,ray,hit);
    RWTexture2D<float4> rays=ResourceDescriptorHeap[probeTrace.x];
    if(hit.hit==0) {
        float3 sky=environment.options.y!=0?SampleEnvironment(direction,environment):ambientIrradiance.rgb/NexusPi;
        rays[uint2(rayIndex,probe)]=float4(clamp(sky,0,65504),ddgi.sampling.z);return;
    }
    // 裏面は負距離で識別し、照明を注入しない。分類・再配置・距離更新に共用する。
    if(hit.front==0) {rays[uint2(rayIndex,probe)]=float4(0,0,0,-max(hit.distance,1e-5));return;}
    float3 position=origin+direction*hit.distance;
    SurfaceData s=MakeSurface(hit.color,hit.metallic,hit.roughness,hit.normal,hit.ao,hit.emissive);
    float3 light=SafeNormalize(lightDirectionIntensity.xyz,float3(0,1,0));
    float visibility=lightDirectionIntensity.w>0 && dot(hit.geometricNormal,light)>0 && !Occluded(Offset(position,hit.geometricNormal,light),light)?1:0;
    float3 radiance=hit.emissive;
    float diffuse=(1-hit.reflectivity)*(1-hit.transmission);
    radiance+=diffuse*EvaluateDirectLighting(s,-direction,light,lightColor.rgb*max(lightDirectionIntensity.w,0),visibility,false);
    // 1本の交差＋前フレーム照度のフィードバックで、フレームをまたいで多重反射を伝播する。
    // 表示用Intensityは履歴へ戻さず、倍率によるエネルギーの発散を防ぐ。
    if(probeTrace.z!=0) radiance+=diffuse*EvaluateDDGILighting(s,-direction,position,ddgi,environment).rgb;
    rays[uint2(rayIndex,probe)]=float4(clamp(radiance,0,65504),hit.distance);
}
#endif
