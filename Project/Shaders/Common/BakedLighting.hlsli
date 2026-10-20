#ifndef NEXUS_BAKED_LIGHTING
#define NEXUS_BAKED_LIGHTING
#include "IndirectLighting.hlsli"
// CPU側80byteと一致。RGBは照度E/π、Aは余弦重み付きの空の可視率。
struct BakedLightingData {
    uint4 textures; float4 originIntensity; float4 spacingEnabled;
    uint4 counts; float4 lightmapTransform;
};
void IrradianceSH(float3 n,out float basis[9]) {
    basis[0]=.2820947918;basis[1]=.4886025119*n.y;basis[2]=.4886025119*n.z;basis[3]=.4886025119*n.x;
    basis[4]=1.0925484306*n.x*n.y;basis[5]=1.0925484306*n.y*n.z;
    basis[6]=.3153915653*(3*n.z*n.z-1);basis[7]=1.0925484306*n.x*n.z;basis[8]=.5462742153*(n.x*n.x-n.y*n.y);
}
float ProbeDistance(float3 direction,uint probe,BakedLightingData b) {
    // D3D cubeの面規約と同じ。境界付近は近傍4点の最小値を取り、薄い壁からの漏れを抑える。
    float3 a=abs(direction);uint face;float2 uv;
    if(a.x>=a.y && a.x>=a.z) { face=direction.x>=0?0:1;uv=float2(direction.x>=0?-direction.z:direction.z,-direction.y)/max(a.x,1e-8); }
    else if(a.y>=a.z) { face=direction.y>=0?2:3;uv=float2(direction.x,direction.y>=0?direction.z:-direction.z)/max(a.y,1e-8); }
    else { face=direction.z>=0?4:5;uv=float2(direction.z>=0?direction.x:-direction.x,-direction.y)/max(a.z,1e-8); }
    Texture2D<float4> depths=ResourceDescriptorHeap[b.textures.z];
    int2 pixel=(int2)floor((uv*.5+.5)*8-.5);float distance=10000;
    [unroll] for(int y=0;y<2;++y) [unroll] for(int x=0;x<2;++x) {
        uint2 p=(uint2)clamp(pixel+int2(x,y),0,7);uint offset=face*64+p.y*8+p.x;
        float4 packed=depths.Load(int3(offset/4,probe,0));distance=min(distance,packed[offset%4]);
    }
    return distance;
}
float4 SampleBakedIrradiance(float3 position,float3 normal,float2 uv,BakedLightingData b) {
    if(b.spacingEnabled.w==0) return 0;
    if(b.lightmapTransform.x>0) {
        Texture2D<float4> atlas=ResourceDescriptorHeap[b.textures.x];SamplerState state=SamplerDescriptorHeap[b.textures.w];
        float4 value=atlas.SampleLevel(state,uv*b.lightmapTransform.xy+b.lightmapTransform.zw,0);
        return float4(max(value.rgb,0),1+saturate(value.a));
    }
    float3 grid=(position-b.originIntensity.xyz)/b.spacingEnabled.xyz;
    if(any(grid<0) || any(grid>float3(b.counts.xyz-1))) return 0;
    uint3 cell=min((uint3)floor(grid),b.counts.xyz-2);float3 fraction=saturate(grid-cell);
    Texture2D<float4> coefficients=ResourceDescriptorHeap[b.textures.y];float basis[9];IrradianceSH(normal,basis);
    float4 sum=0;float total=0;float tolerance=min(b.spacingEnabled.x,min(b.spacingEnabled.y,b.spacingEnabled.z))*.03;
    [unroll] for(uint k=0;k<8;++k) {
        uint3 corner=uint3(k&1,(k>>1)&1,(k>>2)&1),index=cell+corner;
        uint id=index.x+b.counts.x*(index.y+b.counts.y*index.z);
        if(coefficients.Load(int3(9,id,0)).x<.5) continue;
        float3 delta=position-(b.originIntensity.xyz+index*b.spacingEnabled.xyz);
        float lengthToPoint=length(delta);
        if(lengthToPoint>ProbeDistance(delta,id,b)+tolerance) continue;
        float3 weights=lerp(1-fraction,fraction,float3(corner));float weight=weights.x*weights.y*weights.z;
        float4 value=0;[unroll] for(uint l=0;l<9;++l) value+=coefficients.Load(int3(l,id,0))*basis[l];
        sum+=value*weight;total+=weight;
    }
    // グリッド内で有効な点が無い場所を環境光で埋めると壁を透過するため、黒を返す。
    float4 value=total>1e-6?sum/total:0;
    return float4(max(value.rgb,0),1+saturate(value.a));
}
float4 EvaluateBakedLighting(SurfaceData s,float3 v,float3 position,float2 uv,BakedLightingData b,EnvironmentData e) {
    // E/πを保存しているためLambertの1/πを再度掛けない。受光材質とAOは実行時に一度適用する。
    return ApplyDiffuseIrradiance(SampleBakedIrradiance(position,s.normal,uv,b),s,v,e,b.originIntensity.w);
}
#endif
