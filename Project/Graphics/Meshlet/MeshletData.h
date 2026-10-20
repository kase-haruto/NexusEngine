#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include "MeshletConfig.h"
namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * MeshletData
 * - GPU API に依存しない静的 Meshlet と Bounding Sphere を保持する
 * - Primitive は Meshlet 内の local vertex index を3つの8bitへpackする
 *---------------------------------------------------------------------------------------*/
struct Meshlet {
    uint32_t vertexOffset = 0; //< remap 配列の開始位置
    uint32_t vertexCount = 0; //< 出力頂点数
    uint32_t primitiveOffset = 0; //< packed triangle 配列の開始位置
    uint32_t primitiveCount = 0; //< 出力三角形数
};
struct MeshletBounds {
    std::array<float, 3> center {}; //< Mesh local 座標
    float radius = 0; //< Mesh local 半径
};
struct MeshletRange {
    uint32_t first = 0; //< Submesh の最初の Meshlet
    uint32_t count = 0; //< Submesh 内 Meshlet 数
};
struct MeshletData {
    std::vector<Meshlet> meshlets; //< Meshlet metadata
    std::vector<uint32_t> vertices; //< local -> original vertex index
    std::vector<uint32_t> primitives; //< 8bit x 3 packed local indices
    std::vector<MeshletBounds> bounds; //< Meshlet ごとの保守的 Sphere
    std::vector<MeshletRange> submeshes; //< Material 境界を保つ範囲
};
static_assert(sizeof(Meshlet) == 16);
static_assert(sizeof(MeshletBounds) == 16);
// Pack された8bit index と DX12 output / thread / payload 制限を共通設定の変更時も守る。
static_assert(NEXUS_MESHLET_VERTICES > 0 && NEXUS_MESHLET_VERTICES <= 256);
static_assert(NEXUS_MESHLET_PRIMITIVES > 0 && NEXUS_MESHLET_PRIMITIVES <= 256);
static_assert(NEXUS_MESHLET_AS_THREADS > 0 && NEXUS_MESHLET_AS_THREADS <= 128);
static_assert(NEXUS_MESHLET_MS_THREADS > 0 && NEXUS_MESHLET_MS_THREADS <= 128);
static_assert(NEXUS_MESHLET_AS_THREADS * 2 * sizeof(uint32_t) <= 16 * 1024);
}
