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
			auto result = GenerateLightmapGeometry(mesh);
			if(!result) return result;
		}
		return {};
	}
	Result<void> GenerateLightmapGeometry(MeshAssetData& mesh) {
		if(mesh.skinIndex != MeshAssetData::kNoSkin) return {};
		auto prepared = BuildLightmapGeometry(mesh);
		if(!prepared) return std::unexpected(std::move(prepared.error()));
		mesh = std::move(*prepared);
		return {};
	}
	Result<MeshAssetData> BuildLightmapGeometry(const MeshAssetData& mesh) {
		const auto invalid = [](const char* message) { return std::unexpected(Error(ErrorCategory::Resource, 91, message)); };
		if(mesh.skinIndex != MeshAssetData::kNoSkin) return mesh;
		if(mesh.vertexStride != sizeof(ModelVertex) || mesh.vertexData.size() % sizeof(ModelVertex))
			return invalid("Lightmap geometry requires ModelVertex.");
		if(mesh.indexFormat != IndexFormat::UInt16 && mesh.indexFormat != IndexFormat::UInt32)
			return invalid("Invalid lightmap index format.");
		const size_t indexSize = mesh.indexFormat == IndexFormat::UInt16 ? 2 : 4;
		uint64_t corners = 0;
		for(const auto& sub : mesh.submeshes) {
			if(sub.indexCount % 3) return invalid("Lightmap requires triangles.");
			if(uint64_t(sub.firstIndex) + sub.indexCount > mesh.indexData.size() / indexSize)
				return invalid("Lightmap index range invalid.");
			corners += sub.indexCount;
		}
		if(!corners) return mesh;
		if(corners > UINT32_MAX || corners > SIZE_MAX / sizeof(ModelVertex))
			return invalid("Lightmap geometry exceeds buffer limits.");
		const auto grid = LightmapChartGrid(static_cast<uint32_t>(corners / 3));
		MeshAssetData output;
		output.vertexStride = sizeof(ModelVertex);
		output.vertexData.resize(size_t(corners) * sizeof(ModelVertex));
		output.indexFormat = IndexFormat::UInt32;
		output.indexData.resize(size_t(corners) * sizeof(uint32_t));
		output.submeshes.reserve(mesh.submeshes.size());
		uint32_t vertexCount = 0, triangle = 0;
		for(const auto& sub : mesh.submeshes) {
			const uint32_t first = vertexCount;
			for(uint32_t i = 0; i < sub.indexCount; ++i) {
				uint32_t index = 0;
				const size_t address = (size_t(sub.firstIndex) + i) * indexSize;
				std::memcpy(&index, mesh.indexData.data() + address, indexSize);
				const int64_t offset = int64_t(index) + sub.vertexOffset;
				if(offset < 0 || uint64_t(offset) >= mesh.vertexData.size() / sizeof(ModelVertex))
					return invalid("Lightmap vertex range invalid.");
				ModelVertex vertex;
				std::memcpy(&vertex, mesh.vertexData.data() + size_t(offset) * sizeof(vertex), sizeof(vertex));
				// Use the middle half of each chart; leave room for border dilation.
				const uint32_t corner = i % 3;
				vertex.lightmapUV[0] = (float(triangle % grid[0]) + .25f + (corner == 1 ? .5f : 0)) / grid[0];
				vertex.lightmapUV[1] = (float(triangle / grid[0]) + .25f + (corner == 2 ? .5f : 0)) / grid[1];
				std::memcpy(output.vertexData.data() + size_t(vertexCount) * sizeof(vertex), &vertex, sizeof(vertex));
				std::memcpy(output.indexData.data() + size_t(vertexCount) * sizeof(uint32_t), &vertexCount, sizeof(vertexCount));
				++vertexCount;
				if(corner == 2) ++triangle;
			}
			output.submeshes.push_back({first, sub.indexCount, 0, sub.materialSlot});
		}
		return output;
	}
} // namespace NexusEngine
