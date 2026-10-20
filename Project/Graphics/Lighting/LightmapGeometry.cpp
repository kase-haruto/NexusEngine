#include "LightmapGeometry.h"
#include <cmath>
#include <cstring>
namespace NexusEngine {
	std::array<uint32_t, 2> LightmapChartGrid(uint32_t count) {
		const auto columns = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(count))));
		return {columns, columns ? (count + columns - 1) / columns : 0};
	}
	Result<void> GenerateLightmapGeometry(ModelAssetData& asset) {
		for(auto& mesh : asset.meshes) {
			if(mesh.skinIndex != MeshAssetData::kNoSkin) continue;
			if(mesh.vertexStride != sizeof(ModelVertex)) return std::unexpected(Error(ErrorCategory::Resource, 91, "Lightmap geometry requires ModelVertex."));
			uint32_t triangles = 0;
			for(const auto& sub : mesh.submeshes) {
				if(sub.indexCount % 3) return std::unexpected(Error(ErrorCategory::Resource, 91, "Lightmap requires triangles."));
				triangles += sub.indexCount / 3;
			}
			if(!triangles) continue;
			const auto				 grid	   = LightmapChartGrid(triangles);
			const uint32_t			 indexSize = mesh.indexFormat == IndexFormat::UInt16 ? 2 : 4;
			std::vector<ModelVertex> vertices;
			vertices.reserve(size_t(triangles) * 3);
			std::vector<SubmeshRange> ranges;
			uint32_t				  triangle = 0;
			for(const auto& sub : mesh.submeshes) {
				const uint32_t first = static_cast<uint32_t>(vertices.size());
				for(uint32_t i = 0; i < sub.indexCount; ++i) {
					uint32_t	 index	 = 0;
					const size_t address = size_t(sub.firstIndex + i) * indexSize;
					if(address + indexSize > mesh.indexData.size()) return std::unexpected(Error(ErrorCategory::Resource, 91, "Lightmap index range invalid."));
					std::memcpy(&index, mesh.indexData.data() + address, indexSize);
					const int64_t offset = int64_t(index) + sub.vertexOffset;
					if(offset < 0 || uint64_t(offset + 1) * sizeof(ModelVertex) > mesh.vertexData.size()) return std::unexpected(Error(ErrorCategory::Resource, 91, "Lightmap vertex range invalid."));
					ModelVertex vertex;
					std::memcpy(&vertex, mesh.vertexData.data() + size_t(offset) * sizeof(vertex), sizeof(vertex));
					// chartの中央半分を三角形に割り当て、残りを境界の拡張に使う。
					const uint32_t corner = i % 3;
					vertex.lightmapUV[0]  = (float(triangle % grid[0]) + .25f + (corner == 1 ? .5f : 0)) / grid[0];
					vertex.lightmapUV[1]  = (float(triangle / grid[0]) + .25f + (corner == 2 ? .5f : 0)) / grid[1];
					vertices.push_back(vertex);
					if(corner == 2) ++triangle;
				}
				ranges.push_back({first, sub.indexCount, 0, sub.materialSlot});
			}
			mesh.vertexData.resize(vertices.size() * sizeof(ModelVertex));
			std::memcpy(mesh.vertexData.data(), vertices.data(), mesh.vertexData.size());
			mesh.indexFormat = IndexFormat::UInt32;
			mesh.indexData.resize(vertices.size() * 4);
			for(uint32_t i = 0; i < vertices.size(); ++i) std::memcpy(mesh.indexData.data() + size_t(i) * 4, &i, 4);
			mesh.submeshes = std::move(ranges);
		}
		return {};
	}
} // namespace NexusEngine
