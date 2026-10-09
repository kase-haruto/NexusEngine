#include "MeshletCommon.hlsli"
// 既存 Material / Lighting / Shadow / Optics の Pixel 処理を共有する。
#define main TraditionalPixelMain
#include "../Primitive/Primitive.PS.hlsl"
#undef main
float4 main(MeshOutput input) : SV_TARGET {
    if(debugMode == NEXUS_MESHLET_DEBUG_COLOR) {
        // Meshlet ID のみを入力とする決定的 hash。frame によって変化しない。
        uint hash = input.meshletID + 0x9e3779b9u;
        hash ^= hash >> 16; hash *= 0x7feb352du;
        hash ^= hash >> 15; hash *= 0x846ca68bu; hash ^= hash >> 16;
        return float4(0.2f + 0.8f * float3(hash & 255, (hash >> 8) & 255, (hash >> 16) & 255) / 255.0f, 1);
    }
    if(debugMode == NEXUS_MESHLET_DEBUG_CULLING_RESULT) return input.visible != 0 ? float4(0.1f, 0.9f, 0.2f, 1) : float4(1, 0.1f, 0.1f, 1);
    PSInput surface;
    surface.position = input.position; surface.worldNormal = input.worldNormal;
    surface.texcoord = input.texcoord; surface.color = input.color;
    surface.worldPosition = input.worldPosition; surface.worldTangent = input.worldTangent;
    return TraditionalPixelMain(surface);
}
