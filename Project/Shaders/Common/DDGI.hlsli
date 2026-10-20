#ifndef NEXUS_DDGI
#define NEXUS_DDGI
#include "IndirectLighting.hlsli"
struct DDGIData {
    uint4 textures;float4 originIntensity;float4 spacingEnabled;uint4 counts;float4 sampling;
};
float2 DDGISign(float2 v) {return float2(v.x>=0?1:-1,v.y>=0?1:-1);}
float2 DDGIOctEncode(float3 d) {
    d/=max(abs(d.x)+abs(d.y)+abs(d.z),1e-8);
    return d.z>=0?d.xy:(1-abs(d.yx))*DDGISign(d.xy);
}
float3 DDGIOctDecode(float2 p) {
    float3 d=float3(p,1-abs(p.x)-abs(p.y));
    if(d.z<0) d.xy=(1-abs(d.yx))*DDGISign(d.xy);
    return normalize(d);
}
uint3 DDGICoordinates(uint id,DDGIData d) {return uint3(id%d.counts.x,(id/d.counts.x)%d.counts.y,id/(d.counts.x*d.counts.y));}
uint2 DDGITile(uint id,DDGIData d) {return uint2(id%(d.counts.x*d.counts.y),id/(d.counts.x*d.counts.y));}
float3 DDGIPosition(uint id,DDGIData d) {
    float3 offset=0;
    if(d.spacingEnabled.w!=0) {Texture2D<float4> states=ResourceDescriptorHeap[d.textures.z];offset=states.Load(int3(id,0,0)).xyz;}
    return d.originIntensity.xyz+DDGICoordinates(id,d)*d.spacingEnabled.xyz+offset;
}
float3 DDGIRayDirection(uint ray,uint count,uint phase) {
    // 球面Fibonacci列。固定方向を既定とし、任意の回転も再現可能な位相で行う。
    float y=1-2*(ray+.5)/count,angle=6.28318530718*frac(ray*.61803398875+phase*.7548776662);
    float r=sqrt(max(0,1-y*y));return float3(r*cos(angle),y,r*sin(angle));
}
float2 DDGIAtlasUV(uint id,float3 direction,DDGIData d) {
    float2 dimensions=float2(d.counts.x*d.counts.y,d.counts.z)*10;
    return (DDGITile(id,d)*10+1+(DDGIOctEncode(direction)*.5+.5)*8)/dimensions;
}
float4 DDGISampleIrradiance(float3 position,float3 normal,float3 view,DDGIData d) {
    if(d.spacingEnabled.w==0) return 0;
    float3 grid=(position-d.originIntensity.xyz)/d.spacingEnabled.xyz;
    if(any(grid<-.5) || any(grid>float3(d.counts.xyz)-.5)) return 0;
    grid=clamp(grid,0,float3(d.counts.xyz-1));
    uint3 cell=min((uint3)floor(grid),d.counts.xyz-2);float3 fraction=saturate(grid-cell);
    Texture2D<float4> irradiance=ResourceDescriptorHeap[d.textures.x];
    Texture2D<float2> moments=ResourceDescriptorHeap[d.textures.y];
    Texture2D<float4> states=ResourceDescriptorHeap[d.textures.z];
    SamplerState state=SamplerDescriptorHeap[d.textures.w];
    float3 biased=position+normal*d.sampling.x+view*d.sampling.y;
    float4 sum=0;float total=0;
    // Majercikら(2019): 距離の1次・2次モーメントによる可視性付き三線形補間。
    [loop] for(uint k=0;k<8;++k) {
        uint3 corner=uint3(k&1,(k>>1)&1,(k>>2)&1),p=cell+corner;
        uint id=p.x+d.counts.x*(p.y+d.counts.y*p.z);
        float4 probeState=states.Load(int3(id,0,0));if(probeState.w<.5) continue;
        float3 probe=d.originIntensity.xyz+p*d.spacingEnabled.xyz+probeState.xyz;
        float3 delta=biased-probe;float distance=length(delta),visibility=1;
        float3 direction=distance>1e-6?delta/distance:normal;
        float2 m=moments.SampleLevel(state,DDGIAtlasUV(id,direction,d),0);
        float variance=max(m.y-m.x*m.x,1e-5);
        float excess=max(distance-m.x,0);
        visibility=pow(saturate(variance/(variance+excess*excess)),3);
        float facing=pow(saturate(dot(normal,-direction)*.5+.5),2)+.02;
        float3 tri=lerp(1-fraction,fraction,float3(corner));
        float weight=tri.x*tri.y*tri.z*facing*visibility;
        // 極小の可視率を正規化で持ち上げず、遮蔽の信頼度を保つ。
        if(weight<.2) weight*=weight*weight/(.2*.2);
        sum+=irradiance.SampleLevel(state,DDGIAtlasUV(id,normal,d),0)*weight;total+=weight;
    }
    float4 value=total>1e-8?sum/total:0;
    return float4(max(value.rgb,0),1+saturate(value.a));
}
float4 EvaluateDDGILighting(SurfaceData s,float3 v,float3 position,DDGIData d,EnvironmentData e) {
    return ApplyDiffuseIrradiance(DDGISampleIrradiance(position,s.normal,v,d),s,v,e,d.originIntensity.w);
}
#endif
