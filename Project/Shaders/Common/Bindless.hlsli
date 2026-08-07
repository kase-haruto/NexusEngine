#ifndef NEXUS_BINDLESS_HLSLI
#define NEXUS_BINDLESS_HLSLI

// ShaderResourceRefのCPU側indexを使用して、SM 6.6 Direct Heap IndexingからResourceを取得する。
// index 0はEngineが予約したnull Descriptorなので、未設定Resourceの安全な既定値として使用できる。
Texture2D<float4> NexusGetTexture2D(uint resourceIndex)
{
    // Resource indexはCPU側ShaderResourceRef.indexからConstant/StructuredBuffer等で渡す。
    // CPU/GPU Descriptor HandleをShaderデータへ格納する必要はない。
    return ResourceDescriptorHeap[resourceIndex];
}

SamplerState NexusGetSampler(uint samplerIndex)
{
    // SamplerはResource Heapとindex空間を共有しないため、Sampler用indexを個別に渡す。
    return SamplerDescriptorHeap[samplerIndex];
}

#endif
