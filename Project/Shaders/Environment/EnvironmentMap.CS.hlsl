#include "../Common/BRDF.hlsli"
cbuffer Processing : register(b0) {
    uint sourceIndex, outputIndex, faceSize, sampleCount;
    float roughness; uint sourceSize, sourceMip; uint reserved;
};
SamplerState linearClamp : register(s0);
SamplerState panoramaSampler : register(s1);
float2 Hammersley(uint i, uint count) { return float2(float(i) / count, reversebits(i) * 2.3283064365386963e-10); }
float3 CubeDirection(uint face, float2 uv) {
    float2 p = uv * 2 - 1;
    switch(face) {
        case 0: return normalize(float3(1, -p.y, -p.x));
        case 1: return normalize(float3(-1, -p.y, p.x));
        case 2: return normalize(float3(p.x, 1, p.y));
        case 3: return normalize(float3(p.x, -1, -p.y));
        case 4: return normalize(float3(p.x, -p.y, 1));
        default: return normalize(float3(-p.x, -p.y, -1));
    }
}
float3 ToWorld(float3 h, float3 n) {
    float3 t = normalize(cross(abs(n.z) < 0.999 ? float3(0,0,1) : float3(1,0,0), n));
    return t * h.x + cross(n, t) * h.y + n * h.z;
}
float3 SampleGGX(float2 xi, float r, float3 n) {
    float a = max(r * r, 0.0025), a2 = a * a;
    float cosTheta = sqrt((1 - xi.y) / (1 + (a2 - 1) * xi.y));
    float sinTheta = sqrt(saturate(1 - cosTheta * cosTheta));
    return ToWorld(float3(cos(2 * NexusPi * xi.x) * sinTheta, sin(2 * NexusPi * xi.x) * sinTheta, cosTheta), n);
}
[numthreads(8,8,1)]
void Convert(uint3 id : SV_DispatchThreadID) {
    if(any(id.xy >= faceSize)) return;
    Texture2D<float4> panorama = ResourceDescriptorHeap[sourceIndex];
    RWTexture2DArray<float4> output = ResourceDescriptorHeap[outputIndex];
    float3 d = CubeDirection(id.z, (id.xy + 0.5) / faceSize);
    // +X=u .5, +Z=u .75, +Y=top. Matches D3D cube face convention.
    float2 uv = float2(atan2(d.z, d.x) / (2 * NexusPi) + 0.5, acos(clamp(d.y,-1,1)) / NexusPi);
    output[id] = float4(panorama.SampleLevel(panoramaSampler, uv, 0).rgb, 1);
}
[numthreads(8,8,1)]
void Downsample(uint3 id : SV_DispatchThreadID) {
    if(any(id.xy >= faceSize)) return;
    TextureCube<float4> source = ResourceDescriptorHeap[sourceIndex];
    RWTexture2DArray<float4> output = ResourceDescriptorHeap[outputIndex];
    float3 sum = 0;
    // Four source texels per destination; cube sampling crosses face seams.
    for(uint y=0;y<2;++y) for(uint x=0;x<2;++x) {
        float2 uv = (float2(id.xy) + (float2(x,y) + 0.5) * 0.5) / faceSize;
        sum += source.SampleLevel(linearClamp, CubeDirection(id.z, uv), sourceMip).rgb;
    }
    output[id] = float4(sum * 0.25, 1);
}
[numthreads(8,8,1)]
void Irradiance(uint3 id : SV_DispatchThreadID) {
    if(any(id.xy >= faceSize)) return;
    TextureCube<float4> source = ResourceDescriptorHeap[sourceIndex];
    RWTexture2DArray<float4> output = ResourceDescriptorHeap[outputIndex];
    float3 n = CubeDirection(id.z, (id.xy + 0.5) / faceSize), sum = 0;
    for(uint i=0;i<sampleCount;++i) {
        float2 xi = Hammersley(i, sampleCount);
        float phi = 2 * NexusPi * xi.x;
        float3 l = ToWorld(float3(sqrt(xi.y) * cos(phi), sqrt(xi.y) * sin(phi), sqrt(1 - xi.y)), n);
        float pdf = max(dot(n,l) / NexusPi, 1e-5);
        float lod = max(0.0, 0.5 * log2(6.0 * sourceSize * sourceSize / (4 * NexusPi * sampleCount * pdf)));
        sum += source.SampleLevel(linearClamp, l, lod).rgb;
    }
    // Cosine PDF cancels NdotL / pi. Store E, not E/pi.
    output[id] = float4(min(sum * (NexusPi / sampleCount), 65504), 1);
}
[numthreads(8,8,1)]
void Prefilter(uint3 id : SV_DispatchThreadID) {
    if(any(id.xy >= faceSize)) return;
    TextureCube<float4> source = ResourceDescriptorHeap[sourceIndex];
    RWTexture2DArray<float4> output = ResourceDescriptorHeap[outputIndex];
    float3 n = CubeDirection(id.z, (id.xy + 0.5) / faceSize), sum = 0;
    if(roughness == 0) { output[id] = source.SampleLevel(linearClamp, n, 0); return; }
    float weight = 0;
    for(uint i=0;i<sampleCount;++i) {
        float3 h = SampleGGX(Hammersley(i, sampleCount), roughness, n);
        float3 l = reflect(-n, h); float nl = saturate(dot(n, l)), nh = saturate(dot(n,h));
        if(nl > 0) {
            float pdf = max(DistributionGGX(nh, roughness) * nh / max(4 * dot(n,h), 1e-6), 1e-6);
            float lod = max(0.0, 0.5 * log2(6.0 * sourceSize * sourceSize / (4 * NexusPi * sampleCount * pdf)));
            sum += source.SampleLevel(linearClamp, l, lod).rgb * nl; weight += nl;
        }
    }
    output[id] = float4(sum / max(weight, 1e-6), 1);
}
[numthreads(8,8,1)]
void IntegrateBRDF(uint3 id : SV_DispatchThreadID) {
    if(any(id.xy >= faceSize)) return;
    RWTexture2D<float2> output = ResourceDescriptorHeap[outputIndex];
    float2 uv = (id.xy + 0.5) / faceSize;
    float nv = uv.x, r = uv.y;
    float3 v = float3(sqrt(1 - nv * nv), 0, nv); float2 sum = 0;
    for(uint i=0;i<sampleCount;++i) {
        float3 h = SampleGGX(Hammersley(i, sampleCount), r, float3(0,0,1));
        float3 l = reflect(-v, h); float nl = saturate(l.z), nh = saturate(h.z), vh = saturate(dot(v,h));
        if(nl > 0) {
            // BRDF * NdotL / GGX reflection PDF, same Smith model as direct lighting.
            float g = 4 * VisibilitySmith(nv, nl, r) * nl * vh / max(nh, 1e-6);
            float fc = pow(1-vh,5); sum += float2(1-fc,fc) * g;
        }
    }
    output[id.xy] = sum / sampleCount;
}
