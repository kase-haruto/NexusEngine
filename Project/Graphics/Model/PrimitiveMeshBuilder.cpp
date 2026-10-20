#include "PrimitiveMeshBuilder.h"
#include <cmath>
#include <numbers>
#include <cstring>

namespace NexusEngine {
	namespace {
		MeshAssetData Pack(const std::vector<ModelVertex>& vertices, const std::vector<uint32_t>& indices, uint32_t material) {
			MeshAssetData mesh; mesh.vertexStride = sizeof(ModelVertex); mesh.indexFormat = IndexFormat::UInt32;
			mesh.vertexData.resize(vertices.size() * sizeof(ModelVertex)); mesh.indexData.resize(indices.size() * sizeof(uint32_t));
			std::memcpy(mesh.vertexData.data(), vertices.data(), mesh.vertexData.size());
			std::memcpy(mesh.indexData.data(), indices.data(), mesh.indexData.size());
			mesh.submeshes = {{0, static_cast<uint32_t>(indices.size()), 0, material}}; return mesh;
		}
	}
	Result<MeshAssetData> PrimitiveMeshBuilder::Sphere(float radius, uint32_t rings, uint32_t sectors, uint32_t material) {
		if(!std::isfinite(radius) || radius <= 0 || rings < 3 || sectors < 3 || rings > 256 || sectors > 512)
			return std::unexpected(Error(ErrorCategory::Resource, 70, "Invalid sphere tessellation or radius."));
		std::vector<ModelVertex> vertices; std::vector<uint32_t> indices;
		for(uint32_t ring = 0; ring <= rings; ++ring) {
			const float theta = std::numbers::pi_v<float> * static_cast<float>(ring) / static_cast<float>(rings);
			for(uint32_t sector = 0; sector <= sectors; ++sector) {
				const float phi = 2 * std::numbers::pi_v<float> * static_cast<float>(sector) / static_cast<float>(sectors);
				ModelVertex vertex; vertex.normal[0] = std::sin(theta) * std::cos(phi);
				vertex.normal[1] = std::cos(theta); vertex.normal[2] = std::sin(theta) * std::sin(phi);
				for(size_t i = 0; i < 3; ++i) vertex.position[i] = vertex.normal[i] * radius;
				vertex.texcoord[0] = static_cast<float>(sector) / static_cast<float>(sectors);
				vertex.texcoord[1] = static_cast<float>(ring) / static_cast<float>(rings); vertices.push_back(vertex);
			}
		}
		for(uint32_t ring = 0; ring < rings; ++ring) for(uint32_t sector = 0; sector < sectors; ++sector) {
			const uint32_t a = ring * (sectors + 1) + sector, b = a + 1, c = a + sectors + 1, d = c + 1;
			// Skip collapsed polar triangles; the seam intentionally duplicates UV vertices.
			if(ring != 0) indices.insert(indices.end(), {a, b, c});
			if(ring + 1 != rings) indices.insert(indices.end(), {b, d, c});
		}
		return Pack(vertices, indices, material);
	}
	MeshAssetData PrimitiveMeshBuilder::Box(uint32_t material) {
		std::vector<ModelVertex> vertices; std::vector<uint32_t> indices;
		const Vector3 normals[] {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
		for(const auto& normal : normals) {
			const auto up = std::abs(normal.y) > 0.9f ? Vector3::Right() : Vector3::Up();
			const auto u = Vector3::Cross(up, normal).Normalized(); const auto v = Vector3::Cross(normal, u);
			const uint32_t base = static_cast<uint32_t>(vertices.size());
			const float corners[4][2] {{-1,-1},{1,-1},{1,1},{-1,1}};
			for(const auto& corner : corners) {
				const auto position = (normal + u * corner[0] + v * corner[1]) * 0.5f;
				ModelVertex vertex; vertex.position[0] = position.x; vertex.position[1] = position.y; vertex.position[2] = position.z;
				vertex.normal[0] = normal.x; vertex.normal[1] = normal.y; vertex.normal[2] = normal.z;
				vertex.texcoord[0] = (corner[0] + 1) * 0.5f; vertex.texcoord[1] = (corner[1] + 1) * 0.5f; vertices.push_back(vertex);
			}
			indices.insert(indices.end(), {base, base+1, base+2, base, base+2, base+3});
		}
		return Pack(vertices, indices, material);
	}
}
