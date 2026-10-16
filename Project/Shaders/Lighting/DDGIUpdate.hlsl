#include "../Common/DDGI.hlsli"
cbuffer DDGIUpdateConstants : register(b0) {
    DDGIData previous;
    uint4 outputs; // レイSRV・照度UAV・距離UAV・状態UAV
    float4 filter; // 履歴重み・距離指数・最大距離・最小間隔
    uint4 options; // リセット・方向の位相・再配置・フィードバック
};
uint2 DDGIBorderCoord(uint2 p) {
    // 八面体の接続先を同じDispatch内で積分し、別の境界コピーpassを不要にする。
    if((p.x==0 || p.x==9) && (p.y==0 || p.y==9)) return uint2(p.x==0?8:1,p.y==0?8:1);
    if(p.x==0 || p.x==9) return uint2(p.x==0?1:8,9-p.y);
    if(p.y==0 || p.y==9) return uint2(9-p.x,p.y==0?1:8);
    return p;
}
// 1グループ=1プローブ。レイの読み込み・球面方向・分類を100 texelで共有する。
// Hu et al., SDFDDGI (2020), 3.3節のグループ内サンプル再利用を参考にする。
// ここで共有するのは既存DXRの追跡結果であり、SDFの交差判定は使用しない。
groupshared float4 cachedRays[256];
groupshared float3 cachedDirections[256];
groupshared float4 nextProbeState;
groupshared float probeHistory;
[numthreads(8,8,1)]
void DDGIUpdate(uint3 group : SV_GroupID,uint thread : SV_GroupIndex) {
    uint probe=group.x;
    Texture2D<float4> rays=ResourceDescriptorHeap[outputs.x];
    RWTexture2D<float4> irradiance=ResourceDescriptorHeap[outputs.y];
    RWTexture2D<float2> distance=ResourceDescriptorHeap[outputs.z];
    RWTexture2D<float4> state=ResourceDescriptorHeap[outputs.w];
    for(uint ray=thread;ray<previous.counts.w;ray+=64) {
        cachedRays[ray]=rays.Load(int3(ray,probe,0));
        cachedDirections[ray]=DDGIRayDirection(ray,previous.counts.w,options.y);
    }
    GroupMemoryBarrierWithGroupSync();
    if(thread==0) {
        float nearestFront=filter.z,nearestBack=filter.z;float3 frontDirection=0,backDirection=0;uint backfaces=0;
        for(uint ray=0;ray<previous.counts.w;++ray) {
            float t=abs(cachedRays[ray].a);
            if(cachedRays[ray].a<0) {++backfaces;if(t<nearestBack) {nearestBack=t;backDirection=cachedDirections[ray];}}
            else if(t<nearestFront) {nearestFront=t;frontDirection=cachedDirections[ray];}
        }
        float4 oldState=0;
        if(options.x==0) {Texture2D<float4> states=ResourceDescriptorHeap[previous.textures.z];oldState=states.Load(int3(probe,0,0));}
        float3 offset=oldState.xyz;bool inside=backfaces>previous.counts.w*.25;
        if(options.z!=0) {
            float clearance=filter.w*.1;
            if(inside && nearestBack<filter.z) offset+=backDirection*(nearestBack+clearance);
            else if(nearestFront<clearance) offset-=frontDirection*(clearance-nearestFront);
            float extent=length(offset),limit=filter.w*.45;
            if(extent>limit) offset*=limit/extent;
        }
        bool moved=length(offset-oldState.xyz)>filter.w*.001;
        nextProbeState=float4(offset,!inside && !moved?1:0);
        probeHistory=options.x!=0 || oldState.w<.5 || moved?0:filter.x;
        state[uint2(probe,0)]=nextProbeState;
    }
    GroupMemoryBarrierWithGroupSync();
    float distanceLimit=min(filter.z,length(previous.spacingEnabled.xyz)*1.5);
    // 64スレッドで境界を含む100 texelを担当する。範囲外laneも上の同期には必ず参加する。
    for(uint texel=thread;texel<100;texel+=64) {
        uint2 local=uint2(texel%10,texel/10),pixel=DDGITile(probe,previous)*10+local;
        float3 direction=DDGIOctDecode((float2(DDGIBorderCoord(local))-1+.5)/8*2-1);
        float4 sum=0;float2 moments=0;float distanceWeight=0;
        [loop] for(uint ray=0;ray<previous.counts.w;++ray) {
            float4 sample=cachedRays[ray];bool back=sample.a<0;float t=abs(sample.a);
            float cosine=max(dot(direction,cachedDirections[ray]),0);
            // 一様球面PDF=1/(4π)。照度E/πの推定器なので係数は4/N。
            sum.rgb+=max(sample.rgb,0)*cosine;sum.a+=(t>=filter.z*.999 && !back)?cosine:0;
            float weight=pow(cosine,filter.y),depth=min(t*(back?.2:1),distanceLimit);
            moments+=float2(depth,depth*depth)*weight;distanceWeight+=weight;
        }
        sum*=4.0/previous.counts.w;
        moments=distanceWeight>1e-8?moments/distanceWeight:float2(distanceLimit,distanceLimit*distanceLimit);
        float history=probeHistory;
        if(history>0) {
            Texture2D<float4> oldIrradiance=ResourceDescriptorHeap[previous.textures.x];
            Texture2D<float2> oldDistance=ResourceDescriptorHeap[previous.textures.y];
            float4 old=oldIrradiance.Load(int3(pixel,0));
            // 大きな照明変化は履歴を弱める。通常時は高い履歴重みで微小変動を抑える。
            float delta=max(abs(sum.r-old.r),max(abs(sum.g-old.g),abs(sum.b-old.b)));
            if(delta>max(.2,max(old.r,max(old.g,old.b))*.5)) history=min(history,.8);
            sum=lerp(sum,old,history);moments=lerp(moments,oldDistance.Load(int3(pixel,0)),history);
        }
        irradiance[pixel]=float4(clamp(sum.rgb,0,65504),saturate(sum.a));distance[pixel]=moments;
    }
}
