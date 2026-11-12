#include "PhysicsMeshCooking.h"
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include "Graphics/Model/ModelAssetData.h"
#include "Graphics/Model/ModelInstance.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace NexusEngine {
Result<CookedPhysicsMesh> CookPhysicsMesh(const ModelAssetData& asset,int shape,const std::array<float,3>& scale) {
    const auto invalid=[](const char* text) { return std::unexpected(Error(ErrorCategory::Scene,260,text)); };
    if((shape!=3 && shape!=4) || asset.nodes.empty() || asset.meshes.empty()) return invalid("Mesh Collider requires a nonempty CPU model.");
    ModelInstance pose; auto initialized=pose.Initialize(asset); if(!initialized) return std::unexpected(initialized.error());
    JPH::TriangleList triangles;
    for(size_t nodeIndex=0;nodeIndex<asset.nodes.size();++nodeIndex) {
        const auto& matrix=pose.GetNodeWorldTransforms()[nodeIndex];
        const double determinant=double(matrix.At(0,0))*(double(matrix.At(1,1))*matrix.At(2,2)-double(matrix.At(1,2))*matrix.At(2,1))-
            double(matrix.At(0,1))*(double(matrix.At(1,0))*matrix.At(2,2)-double(matrix.At(1,2))*matrix.At(2,0))+
            double(matrix.At(0,2))*(double(matrix.At(1,0))*matrix.At(2,1)-double(matrix.At(1,1))*matrix.At(2,0));
        for(auto meshIndex:asset.nodes[nodeIndex].meshIndices) {
            if(meshIndex>=asset.meshes.size()) return invalid("Mesh Collider has an invalid node mesh index.");
            const auto& mesh=asset.meshes[meshIndex];
            if(mesh.vertexStride!=sizeof(ModelVertex) || mesh.vertexData.size()%sizeof(ModelVertex) || mesh.skinIndex!=MeshAssetData::kNoSkin ||
                (mesh.indexFormat!=IndexFormat::UInt16 && mesh.indexFormat!=IndexFormat::UInt32)) return invalid("Mesh Collider requires static ModelVertex geometry.");
            const size_t indexSize=mesh.indexFormat==IndexFormat::UInt16 ? 2u : 4u;
            if(mesh.indexData.size()%indexSize) return invalid("Mesh Collider index bytes are truncated.");
            for(const auto& sub:mesh.submeshes) {
                if(sub.indexCount%3 || uint64_t(sub.firstIndex)+sub.indexCount>mesh.indexData.size()/indexSize || triangles.size()+sub.indexCount/3>250000)
                    return invalid("Mesh Collider triangle range is invalid or exceeds 250000 triangles.");
                for(uint32_t index=0;index<sub.indexCount;index+=3) {
                    JPH::Vec3 points[3];
                    for(size_t corner=0;corner<3;++corner) {
                        uint32_t vertexIndex=0;
                        std::memcpy(&vertexIndex,mesh.indexData.data()+(size_t(sub.firstIndex)+index+corner)*indexSize,indexSize);
                        const auto adjusted=int64_t(vertexIndex)+sub.vertexOffset;
                        if(adjusted<0 || uint64_t(adjusted)>=mesh.vertexData.size()/sizeof(ModelVertex)) return invalid("Mesh Collider vertex index is out of range.");
                        ModelVertex vertex; std::memcpy(&vertex,mesh.vertexData.data()+size_t(adjusted)*sizeof(vertex),sizeof(vertex));
                        std::array<float,3> point{};
                        // Rendererと同じrow-vectorのnode bind poseを評価し、Entity scaleを形状へ焼き込む。
                        for(size_t axis=0;axis<3;++axis) {
                            const double value=(double(vertex.position[0])*matrix.At(0,axis)+double(vertex.position[1])*matrix.At(1,axis)+
                                double(vertex.position[2])*matrix.At(2,axis)+matrix.At(3,axis))*scale[axis];
                            if(!std::isfinite(value) || std::abs(value)>100000) return invalid("Mesh Collider vertex is outside supported coordinates.");
                            point[axis]=static_cast<float>(value);
                        }
                        points[corner]=JPH::Vec3(point[0],point[1],point[2]);
                    }
                    // 反転したnodeの三角形向きを戻し、閉じたmeshの外面を維持する。
                    if(determinant<0) std::swap(points[1],points[2]);
                    triangles.emplace_back(points[0],points[1],points[2]);
                }
            }
        }
    }
    if(triangles.empty()) return invalid("Mesh Collider contains no triangles.");
    JPH::ShapeSettings::ShapeResult cooked;
    if(shape==3) cooked=JPH::MeshShapeSettings(triangles).Create();
    else {
        std::vector<std::array<float,3>> points; points.reserve(triangles.size()*3);
        for(const auto& triangle:triangles) for(const auto& point:triangle.mV) points.push_back({point.x,point.y,point.z});
        std::ranges::sort(points); points.erase(std::unique(points.begin(),points.end()),points.end());
        if(points.size()<4 || points.size()>4096) return invalid("Convex Collider requires 4 to 4096 unique vertices; use a simplified collision mesh.");
        JPH::Array<JPH::Vec3> vertices; vertices.reserve(points.size());
        for(const auto& point:points) vertices.emplace_back(point[0],point[1],point[2]);
        cooked=JPH::ConvexHullShapeSettings(vertices).Create();
    }
    if(cooked.HasError()) return std::unexpected(Error(ErrorCategory::Scene,260,std::string("Physics mesh cooking failed: ")+cooked.GetError().c_str()));
    return CookedPhysicsMesh{JPH::ShapeRefC(cooked.Get()),static_cast<uint32_t>(triangles.size())};
}
}
