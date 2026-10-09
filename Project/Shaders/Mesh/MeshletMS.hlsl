#include "MeshletCommon.hlsli"
[outputtopology("triangle")]
[numthreads(NEXUS_MESHLET_MS_THREADS, 1, 1)]
void main(uint lane : SV_GroupIndex, uint3 group : SV_GroupID,
    in payload MeshletPayload payload,
    out vertices MeshOutput outputVertices[NEXUS_MESHLET_VERTICES],
    out indices uint3 outputIndices[NEXUS_MESHLET_PRIMITIVES]) {
    // MS の group.x は compact された Payload slot。元 Meshlet ID に展開する。
    uint id = payload.meshletIndices[group.x];
    Meshlet meshlet = meshlets[id];
    // 出力配列への書き込み前に、一様な制御フローで実出力数を1回だけ設定する。
    SetMeshOutputCounts(meshlet.vertexCount, meshlet.primitiveCount);
    for(uint v = lane; v < meshlet.vertexCount; v += NEXUS_MESHLET_MS_THREADS) {
        uint original = vertexIndices[meshlet.vertexOffset + v];
        MeshVertex vertex = vertexData[original];
        MeshOutput result;
        result.position = mul(float4(vertex.position, 1), worldViewProjection);
        result.worldNormal = normalize(mul(vertex.normal, (float3x3)worldInverseTranspose));
        result.worldPosition = mul(float4(vertex.position, 1), world).xyz;
        result.worldTangent = float4(mul(vertex.tangent.xyz, (float3x3)world),
            vertex.tangent.w * (determinant((float3x3)world) < 0 ? -1 : 1));
        result.texcoord = vertex.texcoord; result.color = vertex.color;
        result.meshletID = id; result.visible = payload.visibility[group.x];
        outputVertices[v] = result;
    }
    for(uint p = lane; p < meshlet.primitiveCount; p += NEXUS_MESHLET_MS_THREADS) {
        uint packed = primitiveIndices[meshlet.primitiveOffset + p];
        // 3つのlocal indexを丸ごと書く。original vertex index ではない。
        outputIndices[p] = uint3(packed & 255, (packed >> 8) & 255, (packed >> 16) & 255);
    }
}
