#include "ModelThumbnail.h"
#include "ModelInstance.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <limits>
namespace NexusEngine {
namespace {
constexpr uint32_t size = 160;
DecodedImage Background() {
    DecodedImage image{size, size, std::vector<uint8_t>(size * size * 4)};
    for(uint32_t y = 0; y < size; ++y) for(uint32_t x = 0; x < size; ++x) {
        auto* pixel = image.rgbaPixels.data() + (y * size + x) * 4;
        pixel[0] = pixel[1] = pixel[2] = ((x / 10 + y / 10) % 2) ? 35 : 43; pixel[3] = 255;
    }
    return image;
}
Vector3 Transform(Vector3 p, const Matrix4x4& m) {
    return {p.x*m.At(0,0)+p.y*m.At(1,0)+p.z*m.At(2,0)+m.At(3,0),
        p.x*m.At(0,1)+p.y*m.At(1,1)+p.z*m.At(2,1)+m.At(3,1),
        p.x*m.At(0,2)+p.y*m.At(1,2)+p.z*m.At(2,2)+m.At(3,2)};
}
Vector3 View(Vector3 p) { return {(p.x-p.z)*.707107f, p.y*.866025f-(p.x+p.z)*.353553f, p.y*.5f+(p.x+p.z)*.612372f}; }
float Edge(Vector3 a, Vector3 b, Vector3 p) { return (b.x-a.x)*(p.y-a.y)-(b.y-a.y)*(p.x-a.x); }
}
DecodedImage CreateImageThumbnail(const DecodedImage& source) {
    auto result = Background();
    if(!source.width || !source.height) return result;
    const float scale = std::min(static_cast<float>(size)/source.width, static_cast<float>(size)/source.height);
    const auto width = std::max(1U, static_cast<uint32_t>(source.width*scale));
    const auto height = std::max(1U, static_cast<uint32_t>(source.height*scale));
    for(uint32_t y = 0; y < height; ++y) for(uint32_t x = 0; x < width; ++x) {
        const auto sx = std::min(source.width-1, static_cast<uint32_t>(x/scale));
        const auto sy = std::min(source.height-1, static_cast<uint32_t>(y/scale));
        const auto* pixel = source.rgbaPixels.data()+(sy*source.width+sx)*4;
        auto* target = result.rgbaPixels.data()+((y+(size-height)/2)*size+x+(size-width)/2)*4;
        for(size_t c=0;c<3;++c) target[c]=static_cast<uint8_t>((pixel[c]*pixel[3]+target[c]*(255-pixel[3]))/255);
    }
    return result;
}
Result<DecodedImage> CreateModelThumbnail(const ModelAssetData& asset, int selectedNode) {
    ModelInstance pose;
    auto initialized = pose.Initialize(asset);
    if(!initialized) return std::unexpected(initialized.error());
    struct Triangle { Vector3 p[3]; std::array<float,4> color; };
    std::vector<Triangle> triangles;
    Vector3 low{1e30f,1e30f,1e30f}, high{-1e30f,-1e30f,-1e30f};
    for(size_t node=0;node<asset.nodes.size();++node) {
        if(selectedNode >= 0 && node != static_cast<size_t>(selectedNode)) continue;
        for(auto meshIndex : asset.nodes[node].meshIndices) {
            if(meshIndex >= asset.meshes.size()) continue;
            const auto& mesh=asset.meshes[meshIndex];
            if(mesh.vertexStride != sizeof(ModelVertex)) continue;
            for(const auto& part : mesh.submeshes) for(uint32_t first=0;first+2<part.indexCount;first+=3) {
                Triangle triangle{}; triangle.color={.7f,.75f,.8f,1};
                if(part.materialSlot < asset.materials.size()) triangle.color=asset.materials[part.materialSlot].baseColorFactor;
                bool valid=true;
                for(size_t corner=0;corner<3;++corner) {
                    const size_t index=static_cast<size_t>(part.firstIndex)+first+corner;
                    const size_t stride=mesh.indexFormat==IndexFormat::UInt16?2:4;
                    if((index+1)*stride>mesh.indexData.size()) { valid=false; break; }
                    uint32_t vertex=0;
                    if(stride==2) { uint16_t value; std::memcpy(&value,mesh.indexData.data()+index*2,2); vertex=value; }
                    else std::memcpy(&vertex,mesh.indexData.data()+index*4,4);
                    const int64_t offset=static_cast<int64_t>(vertex)+part.vertexOffset;
                    if(offset<0 || (static_cast<size_t>(offset)+1)*mesh.vertexStride>mesh.vertexData.size()) { valid=false; break; }
                    ModelVertex input; std::memcpy(&input,mesh.vertexData.data()+static_cast<size_t>(offset)*mesh.vertexStride,sizeof(input));
                    Vector3 p{input.position[0],input.position[1],input.position[2]};
                    if(mesh.skinIndex != MeshAssetData::kNoSkin) {
                        if(const auto* palette=pose.GetSkinPalette(mesh.skinIndex)) {
                            Vector3 skinned{};
                            for(size_t joint=0;joint<4;++joint) {
                                const auto indexJoint=static_cast<size_t>(input.jointIndices[joint]);
                                if(indexJoint<palette->size()) skinned=skinned+Transform(p,(*palette)[indexJoint])*input.jointWeights[joint];
                            }
                            p=skinned;
                        }
                    } else p=Transform(p,pose.GetNodeWorldTransforms()[node]);
                    p=View(p); triangle.p[corner]=p;
                    low={std::min(low.x,p.x),std::min(low.y,p.y),std::min(low.z,p.z)};
                    high={std::max(high.x,p.x),std::max(high.y,p.y),std::max(high.z,p.z)};
                }
                if(valid) triangles.push_back(triangle);
            }
        }
    }
    auto image=Background();
    if(triangles.empty()) return image;
    const auto center=(low+high)*.5f;
    const float scale=140/std::max({high.x-low.x,high.y-low.y,1e-6f});
    std::vector<float> depth(size*size,std::numeric_limits<float>::max());
    for(auto triangle : triangles) {
        const auto a=triangle.p[1]-triangle.p[0], b=triangle.p[2]-triangle.p[0];
        const Vector3 normal{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
        const float shade=.25f+.75f*std::abs(normal.y*.5f-normal.z*.866f)/std::max(normal.Length(),1e-8f);
        for(auto& p:triangle.p) { p.x=(p.x-center.x)*scale+80; p.y=80-(p.y-center.y)*scale; }
        const auto* p=triangle.p;
        const float area=Edge(p[0],p[1],p[2]); if(std::abs(area)<1e-8f) continue;
        const int x0=std::clamp(static_cast<int>(std::floor(std::min({p[0].x,p[1].x,p[2].x}))),0,159);
        const int x1=std::clamp(static_cast<int>(std::ceil(std::max({p[0].x,p[1].x,p[2].x}))),0,159);
        const int y0=std::clamp(static_cast<int>(std::floor(std::min({p[0].y,p[1].y,p[2].y}))),0,159);
        const int y1=std::clamp(static_cast<int>(std::ceil(std::max({p[0].y,p[1].y,p[2].y}))),0,159);
        for(int y=y0;y<=y1;++y) for(int x=x0;x<=x1;++x) {
            Vector3 point{static_cast<float>(x)+.5f,static_cast<float>(y)+.5f,0};
            const float u=Edge(p[1],p[2],point)/area,v=Edge(p[2],p[0],point)/area,w=1-u-v;
            if(u<0 || v<0 || w<0) continue;
            const float z=u*p[0].z+v*p[1].z+w*p[2].z;
            const size_t pixel=static_cast<size_t>(y)*size+x;
            if(z>=depth[pixel]) continue; depth[pixel]=z;
            for(size_t c=0;c<3;++c) image.rgbaPixels[pixel*4+c]=static_cast<uint8_t>(255*std::sqrt(std::clamp(triangle.color[c]*shade,0.0f,1.0f)));
        }
    }
    return image;
}
}
