#include "MeshletBuilder.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstddef>
#include <limits>
namespace NexusEngine {
static_assert(sizeof(ModelVertex) == 104 && offsetof(ModelVertex, position) == 0 &&
    offsetof(ModelVertex, normal) == 12 && offsetof(ModelVertex, texcoord) == 24 &&
    offsetof(ModelVertex, color) == 32 && offsetof(ModelVertex, jointIndices) == 48 && offsetof(ModelVertex, jointWeights) == 64);
Result<MeshletData> MeshletBuilder::Build(const MeshAssetData& mesh) const {
    const auto invalid = [] { return std::unexpected(Error(ErrorCategory::Resource, 80, "Invalid static meshlet input.")); };
    const size_t indexSize = mesh.indexFormat == IndexFormat::UInt16 ? 2 : 4;
    if((mesh.indexFormat != IndexFormat::UInt16 && mesh.indexFormat != IndexFormat::UInt32) ||
       mesh.skinIndex != MeshAssetData::kNoSkin || mesh.vertexStride != sizeof(ModelVertex) ||
       mesh.vertexData.empty() || mesh.vertexData.size() % sizeof(ModelVertex) != 0 ||
       mesh.indexData.size() % indexSize != 0 || mesh.submeshes.empty()) return invalid();
    const size_t vertexCount = mesh.vertexData.size() / sizeof(ModelVertex);
    const size_t indexCount = mesh.indexData.size() / indexSize;
    if(vertexCount > UINT32_MAX || indexCount > UINT32_MAX) return invalid();
    // Bounds only need positions; copying full skinned/material vertices wastes 92 bytes per vertex.
    std::vector<std::array<float, 3>> input(vertexCount);
    for(size_t i = 0; i < vertexCount; ++i) {
        std::memcpy(input[i].data(), mesh.vertexData.data() + i * sizeof(ModelVertex), sizeof(input[i]));
        for(float p : input[i]) if(!std::isfinite(p)) return invalid();
    }
    MeshletData output;
    uint64_t totalIndices = 0;
    for(const auto& submesh : mesh.submeshes) totalIndices += submesh.indexCount;
    // remap は最悪でも入力 corner 数。32bit GPU offset の overflow を生成前に拒否。
    if(totalIndices > UINT32_MAX) return invalid();
    for(const auto& submesh : mesh.submeshes) {
        if(submesh.indexCount % 3 != 0 || uint64_t(submesh.firstIndex) + submesh.indexCount > indexCount) return invalid();
        MeshletRange range {static_cast<uint32_t>(output.meshlets.size()), 0};
        std::vector<uint32_t> remap, triangles;
        const auto flush = [&] {
            if(triangles.empty()) return;
            Meshlet m {static_cast<uint32_t>(output.vertices.size()), static_cast<uint32_t>(remap.size()),
                       static_cast<uint32_t>(output.primitives.size()), static_cast<uint32_t>(triangles.size())};
            ////////////////////////////////////////////////////////////////////////////
            // AABB 中心から全参照頂点までの最大距離を使う保守的 Bounding Sphere。
            ////////////////////////////////////////////////////////////////////////////
            MeshletBounds b;
            std::array<double, 3> lo, hi;
            for(size_t axis = 0; axis < 3; ++axis) lo[axis] = hi[axis] = input[remap.front()][axis];
            for(uint32_t i : remap) for(size_t axis = 0; axis < 3; ++axis) {
                lo[axis] = (std::min)(lo[axis], double(input[i][axis]));
                hi[axis] = (std::max)(hi[axis], double(input[i][axis]));
            }
            for(size_t axis = 0; axis < 3; ++axis) b.center[axis] = float((lo[axis] + hi[axis]) * 0.5);
            double radius = 0;
            for(uint32_t i : remap) {
                double d2 = 0;
                for(size_t axis = 0; axis < 3; ++axis) { const double d = double(input[i][axis]) - b.center[axis]; d2 += d * d; }
                radius = (std::max)(radius, std::sqrt(d2));
            }
            b.radius = std::nextafter(float(radius), (std::numeric_limits<float>::infinity)());
            output.meshlets.push_back(m); output.bounds.push_back(b);
            output.vertices.insert(output.vertices.end(), remap.begin(), remap.end());
            output.primitives.insert(output.primitives.end(), triangles.begin(), triangles.end());
            remap.clear(); triangles.clear();
        };
        ////////////////////////////////////////////////////////////////////////////
        // 安定した入力順分割。Material 境界を跨がず、頂点を Meshlet 内で重複排除。
        ////////////////////////////////////////////////////////////////////////////
        for(uint32_t triangle = 0; triangle < submesh.indexCount / 3; ++triangle) {
            uint32_t ids[3] {};
            for(uint32_t corner = 0; corner < 3; ++corner) {
                const size_t offset = (size_t(submesh.firstIndex) + triangle * 3 + corner) * indexSize;
                uint32_t raw = 0;
                if(indexSize == 2) { uint16_t value; std::memcpy(&value, mesh.indexData.data() + offset, 2); raw = value; }
                else std::memcpy(&raw, mesh.indexData.data() + offset, 4);
                const int64_t id = int64_t(raw) + submesh.vertexOffset;
                if(id < 0 || uint64_t(id) >= vertexCount) return invalid();
                ids[corner] = static_cast<uint32_t>(id);
            }
            uint32_t added = 0;
            for(uint32_t corner = 0; corner < 3; ++corner) {
                bool duplicate = false;
                for(uint32_t previous = 0; previous < corner; ++previous) duplicate |= ids[previous] == ids[corner];
                if(!duplicate && std::find(remap.begin(), remap.end(), ids[corner]) == remap.end()) ++added;
            }
            if(remap.size() + added > NEXUS_MESHLET_VERTICES || triangles.size() == NEXUS_MESHLET_PRIMITIVES) flush();
            uint32_t packed = 0;
            for(uint32_t corner = 0; corner < 3; ++corner) {
                auto it = std::find(remap.begin(), remap.end(), ids[corner]);
                if(it == remap.end()) { remap.push_back(ids[corner]); it = remap.end() - 1; }
                packed |= static_cast<uint32_t>(it - remap.begin()) << (corner * 8);
            }
            triangles.push_back(packed);
        }
        flush();
        range.count = static_cast<uint32_t>(output.meshlets.size()) - range.first;
        output.submeshes.push_back(range);
    }
    if(output.meshlets.empty() || output.vertices.size() > UINT32_MAX || output.primitives.size() > UINT32_MAX) return invalid();
    for(const auto& b : output.bounds) if(!std::isfinite(b.radius)) return invalid();
    return output;
}
}
