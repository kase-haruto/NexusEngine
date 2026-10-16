#include "../Shaders/Common/PBR.hlsli"
cbuffer TestConstants : register(b0) { EnvironmentData environment; };
RWStructuredBuffer<float4> results : register(u0);
[numthreads(32,1,1)]
void main(uint id : SV_DispatchThreadID) {
    float3 axes[6] = {float3(1,0,0),float3(-1,0,0),float3(0,1,0),float3(0,-1,0),float3(0,0,1),float3(0,0,-1)};
    float3 value = 0;
    if(id < 6) value = SampleEnvironment(axes[id], environment);
    else if(id < 12) {
        TextureCube<float4> map = ResourceDescriptorHeap[environment.textures.y];
        SamplerState state = SamplerDescriptorHeap[environment.options.x];
        value = map.SampleLevel(state, axes[id-6], 0).rgb / NexusPi;
    } else if(id < 21) {
        TextureCube<float4> map = ResourceDescriptorHeap[environment.textures.z];
        SamplerState state = SamplerDescriptorHeap[environment.options.x];
        value = map.SampleLevel(state, float3(1,0,0), id-12).rgb;
    } else {
        SurfaceData s = MakeSurface(0.5.xxx, 0, 0.5, float3(0,0,1), 1, 0);
        float3 v=float3(0,0,1), l=float3(1,0,0);
        if(id==21) {
            Texture2D<float2> lut = ResourceDescriptorHeap[environment.textures.w];
            SamplerState state = SamplerDescriptorHeap[environment.options.x];
            value.xy=lut.SampleLevel(state,float2(1,0.5),0);
        }
        if(id==22) value=EvaluateDirectLighting(s,v,l,1,1,false);
        if(id==23) value=EvaluateDirectLighting(s,v,l,1,1,true);
        if(id==24) { s.emissive=float3(2,3,4); EnvironmentData e=(EnvironmentData)0; value=EvaluatePBRLighting(s,v,l,0,1,e,0); }
        if(id==25 || id==26) {
            if(id==25) s.ambientOcclusion=0;
            IndirectLighting indirect=EvaluateIndirectLighting(s,v,environment,0);
            value=indirect.diffuse+indirect.specular;
        }
        if(id==27) { s.ambientOcclusion=0; value=EvaluateDirectLighting(s,v,v,1,1,false); }
        if(id==28) value=EvaluateDirectLighting(s,v,v,1,1,false);
        if(id==29) value=MaterialMetallicRoughness(float2(0.8,0.7),float4(0.1,0.5,0.25,1)).xyx;
        if(id==30) value=MaterialOcclusion(0.2,0.5).xxx;
        if(id==31) { s.baseColor=0; s.metallic=1; IndirectLighting indirect=EvaluateIndirectLighting(s,v,environment,0); value=indirect.diffuse; }
    }
    results[id]=float4(value,1);
}
