#ifndef NEXUS_MESHLET_COMMON
#define NEXUS_MESHLET_COMMON
#include "MeshletConfig.h"
#include "../Primitive/PrimitiveDrawData.hlsli"
struct Meshlet { uint vertexOffset; uint vertexCount; uint primitiveOffset; uint primitiveCount; };
struct MeshletBounds { float3 center; float radius; };
// ModelVertex と同じ96bytes。StructuredBuffer の各要素は cbuffer packing を使用しない。
struct MeshVertex {
    float3 position; float3 normal; float2 texcoord; float4 color;
    float4 jointIndices; float4 jointWeights;
    float4 tangent;
};
StructuredBuffer<MeshVertex> vertexData : register(t0);
StructuredBuffer<Meshlet> meshlets : register(t1);
StructuredBuffer<uint> vertexIndices : register(t2);
StructuredBuffer<uint> primitiveIndices : register(t3);
StructuredBuffer<MeshletBounds> meshletBounds : register(t4);
cbuffer MeshletCulling : register(b1) {
    float4 frustumPlanes[6];
    uint firstMeshlet; uint meshletCount; uint debugMode; uint enableCulling;
};
struct MeshletPayload {
    uint meshletIndices[NEXUS_MESHLET_AS_THREADS];
    uint visibility[NEXUS_MESHLET_AS_THREADS];
};
struct MeshOutput {
    float4 position : SV_POSITION;
    float3 worldNormal : NORMAL;
    float2 texcoord : TEXCOORD;
    float4 color : COLOR;
    float3 worldPosition : TEXCOORD3;
    float4 worldTangent : TANGENT;
    nointerpolation uint meshletID : TEXCOORD1;
    nointerpolation uint visible : TEXCOORD2;
};
bool IsMeshletVisible(uint id) {
    MeshletBounds bounds = meshletBounds[id];
    for(uint i = 0; i < 6; ++i) {
        float distance = dot(frustumPlanes[i], float4(bounds.center, 1));
        if(distance < -bounds.radius - 1e-5f * max(1.0f, bounds.radius)) return false;
    }
    return true;
}
#endif
