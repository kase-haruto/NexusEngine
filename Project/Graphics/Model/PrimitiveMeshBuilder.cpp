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
    Result<ModelAssetData> PrimitiveMeshBuilder::Build(PrimitiveShape shape) {
        if(!IsValidPrimitiveShape(shape) || shape == PrimitiveShape::Asset)
            return std::unexpected(Error(ErrorCategory::Resource, 70, "Select a built-in primitive shape."));
        MeshAssetData mesh;
        if(shape == PrimitiveShape::Cube) mesh = Box(0);
        else if(shape == PrimitiveShape::Sphere) {
            auto sphere = Sphere(0.5f, 24, 32, 0);
            if(!sphere) return std::unexpected(sphere.error());
            mesh = std::move(*sphere);
        } else {
            std::vector<ModelVertex> vertices;
            std::vector<uint32_t> indices;
            const auto vertex = [&](Vector3 p, Vector3 n, float u, float v) {
                ModelVertex result;
                result.position[0] = p.x; result.position[1] = p.y; result.position[2] = p.z;
                result.normal[0] = n.x; result.normal[1] = n.y; result.normal[2] = n.z;
                result.texcoord[0] = u; result.texcoord[1] = v;
                vertices.push_back(result);
            };
            if(shape == PrimitiveShape::Plane) {
                vertex({-0.5f,0,-0.5f}, {0,1,0}, 0, 0);
                vertex({-0.5f,0, 0.5f}, {0,1,0}, 0, 1);
                vertex({ 0.5f,0, 0.5f}, {0,1,0}, 1, 1);
                vertex({ 0.5f,0,-0.5f}, {0,1,0}, 1, 0);
                indices = {0,1,2,0,2,3};
            } else {
                constexpr uint32_t sectors = 32;
                const bool cone = shape == PrimitiveShape::Cone;
                for(uint32_t i = 0; i < sectors; ++i) {
                    const float u0 = static_cast<float>(i) / sectors, u1 = static_cast<float>(i + 1) / sectors;
                    const float a = u0 * 2 * std::numbers::pi_v<float>, b = u1 * 2 * std::numbers::pi_v<float>;
                    const Vector3 p{0.5f * std::cos(a), -0.5f, 0.5f * std::sin(a)};
                    const Vector3 q{0.5f * std::cos(b), -0.5f, 0.5f * std::sin(b)};
                    const Vector3 n0 = Vector3{std::cos(a), cone ? 0.5f : 0.0f, std::sin(a)}.Normalized();
                    const Vector3 n1 = Vector3{std::cos(b), cone ? 0.5f : 0.0f, std::sin(b)}.Normalized();
                    auto base = static_cast<uint32_t>(vertices.size());
                    vertex(p, n0, u0, 1);
                    if(cone) {
                        const float mid = (a + b) * 0.5f;
                        vertex({0,0.5f,0}, Vector3{std::cos(mid),0.5f,std::sin(mid)}.Normalized(), (u0 + u1) * 0.5f, 0);
                        vertex(q, n1, u1, 1);
                        indices.insert(indices.end(), {base,base+1,base+2});
                    } else {
                        vertex({p.x,0.5f,p.z}, n0, u0, 0);
                        vertex(q, n1, u1, 1);
                        vertex({q.x,0.5f,q.z}, n1, u1, 0);
                        indices.insert(indices.end(), {base,base+1,base+2,base+2,base+1,base+3});
                    }
                    // Separate cap vertices preserve hard normals and planar UVs.
                    base = static_cast<uint32_t>(vertices.size());
                    vertex({0,-0.5f,0}, {0,-1,0}, 0.5f, 0.5f);
                    vertex(p, {0,-1,0}, p.x + 0.5f, p.z + 0.5f);
                    vertex(q, {0,-1,0}, q.x + 0.5f, q.z + 0.5f);
                    indices.insert(indices.end(), {base,base+1,base+2});
                    if(!cone) {
                        base = static_cast<uint32_t>(vertices.size());
                        vertex({0,0.5f,0}, {0,1,0}, 0.5f, 0.5f);
                        vertex({q.x,0.5f,q.z}, {0,1,0}, q.x + 0.5f, q.z + 0.5f);
                        vertex({p.x,0.5f,p.z}, {0,1,0}, p.x + 0.5f, p.z + 0.5f);
                        indices.insert(indices.end(), {base,base+1,base+2});
                    }
                }
            }
            mesh = Pack(vertices, indices, 0);
        }
        ModelAssetData model;
        model.meshes.push_back(std::move(mesh));
        model.materials.emplace_back();
        ModelNodeAssetData node; node.name = "Primitive"; node.meshIndices = {0};
        model.nodes.push_back(std::move(node));
        return model;
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
