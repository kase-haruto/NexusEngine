cbuffer TextureSelection : register(b0) { uint textureIndex; uint samplerIndex; float lod; uint sampleMode; };
float4 VSMain(uint id : SV_VertexID) : SV_POSITION {
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}
float4 PSMain(float4 position : SV_POSITION) : SV_TARGET {
    Texture2D<float4> image = ResourceDescriptorHeap[textureIndex];
    if(sampleMode != 0) {
        SamplerState state = SamplerDescriptorHeap[samplerIndex];
        if(sampleMode == 3) return float4(image.SampleLevel(state, position.xy / 8.0, lod).aaa, 1);
        if(sampleMode == 1) return image.SampleLevel(state, position.xy / 8.0, lod);
        // Land on a black mip-0 texel centre: bilinear filtering alone must not pass.
        return image.Sample(state, position.xy * 8.0 + 0.5 / 64.0);
    }
    return image.Load(int3(0, 0, 0));
}
