cbuffer TextureSelection : register(b0) { uint textureIndex; uint3 padding; };
float4 VSMain(uint id : SV_VertexID) : SV_POSITION {
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}
float4 PSMain() : SV_TARGET {
    Texture2D<float4> image = ResourceDescriptorHeap[textureIndex];
    return image.Load(int3(0, 0, 0));
}
