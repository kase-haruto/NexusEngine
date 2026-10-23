// Filter only stochastic specular radiance, then add it to untouched direct/diffuse/emissive.
#include "../Common/DDGI.hlsli"
cbuffer FilterConstants : register(b0) {
    uint sourceIndex, guideIndex, outputIndex, width;
    uint height, enabled;
};
[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if(id.x >= width || id.y >= height) return;
    Texture2D<float4> source = ResourceDescriptorHeap[sourceIndex];
    Texture2D<float4> guides = ResourceDescriptorHeap[guideIndex];
    RWTexture2D<float4> output = ResourceDescriptorHeap[outputIndex];
    int2 pixel = int2(id.xy);
    float4 center = source.Load(int3(pixel,0));
    if(center.a <= 0) return; // Ideal mirrors, glass and non-metals are preserved exactly.
    float4 guide = guides.Load(int3(pixel,0));
    float3 sum = center.rgb; float total = 1;
    // Preserve sharp polished reflections; increase support on rough surfaces.
    int radius = enabled != 0 && guide.z >= .15 ? 2 : 0;
    float3 n = DDGIOctDecode(guide.xy);
    for(int y=-radius;y<=radius;++y) for(int x=-radius;x<=radius;++x) {
        if(x==0 && y==0) continue;
        int2 p = pixel + int2(x,y);
        if(any(p < 0) || p.x >= int(width) || p.y >= int(height)) continue;
        float4 sample = source.Load(int3(p,0)), neighbor = guides.Load(int3(p,0));
        if(sample.a <= 0 || neighbor.w != guide.w || abs(neighbor.z-guide.z) > .08) continue;
        float depth = abs(sample.a-center.a) / max(center.a,.1);
        if(depth > .035) continue;
        float normalWeight = pow(saturate(dot(n,DDGIOctDecode(neighbor.xy))),64);
        float spatial = exp(-float(x*x+y*y)/max(float(radius*radius),1));
        float weight = spatial * normalWeight * exp(-depth*100);
        sum += sample.rgb * weight; total += weight;
    }
    float4 base = output[pixel];
    output[pixel] = float4(clamp(base.rgb+sum/total,0,65504),base.a);
}
