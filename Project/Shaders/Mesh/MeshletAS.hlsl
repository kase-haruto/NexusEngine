#include "MeshletCommon.hlsli"
groupshared MeshletPayload payload;
groupshared uint visibleCount;
[numthreads(NEXUS_MESHLET_AS_THREADS, 1, 1)]
void main(uint lane : SV_GroupIndex, uint3 group : SV_GroupID) {
    if(lane == 0) visibleCount = 0;
    GroupMemoryBarrierWithGroupSync();
    // 32候補を group 単位で検査。末尾の余剰 thread は Buffer に触れない。
    uint relative = group.x * NEXUS_MESHLET_AS_THREADS + lane;
    if(relative < meshletCount) {
        uint id = firstMeshlet + relative;
        // 無効時の通常表示では Bounds の読込と6平面判定も省略する。
        // CullingResult だけは分類を表示するため、無効設定でも判定を継続する。
        bool visible = true;
        if(enableCulling != 0 || debugMode == NEXUS_MESHLET_DEBUG_CULLING_RESULT)
            visible = IsMeshletVisible(id);
        // CullingResult は Readback せず全候補を描き、判定結果を色として確認する。
        if(enableCulling == 0 || visible || debugMode == NEXUS_MESHLET_DEBUG_CULLING_RESULT) {
            uint slot;
            InterlockedAdd(visibleCount, 1, slot);
            payload.meshletIndices[slot] = id;
            payload.visibility[slot] = visible ? 1 : 0;
        }
    }
    GroupMemoryBarrierWithGroupSync();
    // 全threadが一様に1回呼ぶ。可視0なら MS group も0。Payload は256bytes。
    // Wave intrinsic に依存せず、Wave32 / Wave64 とも同じ group 集約になる。
    DispatchMesh(visibleCount, 1, 1, payload);
}
