#include "Graphics/Model/PrimitiveMeshBuilder.h"
#include <cstring>

void PrimitiveShapeTests() {
    using namespace NexusEngine;
    // Registering models must never give an unassigned renderer geometry.
    ModelAssetData startup; startup.nodes.resize(3);
    ModelAssetLibrary populated("Generated", startup);
    const auto revisionBefore = populated.Revision();
    auto unassigned = populated.Resolve(PrimitiveShape::Asset, "", "missing-texture.png");
    assert(unassigned && unassigned->begin == unassigned->end);
    assert(populated.Revision() == revisionBefore && !populated.Model(""));
    auto legacyRange = populated.Resolve(kStartupModelAssetReference, "");
    assert(legacyRange && legacyRange->begin == 0 && legacyRange->end == 3);
    ModelAssetLibrary library("Generated", {});
    assert(!PrimitiveMeshBuilder::Build(PrimitiveShape::Asset));
    assert(!library.Resolve(static_cast<PrimitiveShape>(99), "", ""));
    for(auto shape : {PrimitiveShape::Cube, PrimitiveShape::Sphere, PrimitiveShape::Plane,
        PrimitiveShape::Cylinder, PrimitiveShape::Cone}) {
        auto model = PrimitiveMeshBuilder::Build(shape);
        assert(model && model->meshes.size() == 1 && model->nodes.size() == 1 && model->materials.size() == 1);
        const auto& mesh = model->meshes.front();
        std::vector<ModelVertex> vertices(mesh.vertexData.size() / sizeof(ModelVertex));
        std::vector<uint32_t> indices(mesh.indexData.size() / sizeof(uint32_t));
        std::memcpy(vertices.data(), mesh.vertexData.data(), mesh.vertexData.size());
        std::memcpy(indices.data(), mesh.indexData.data(), mesh.indexData.size());
        assert(!vertices.empty() && !indices.empty() && indices.size() % 3 == 0);
        for(const auto& v : vertices) {
            for(float p : v.position) assert(std::isfinite(p) && std::abs(p) <= 0.50001f);
            float length = 0; for(float n : v.normal) length += n * n;
            assert(std::abs(length - 1) < 0.00001f);
        }
        for(size_t i = 0; i < indices.size(); i += 3) {
            for(size_t j = 0; j < 3; ++j) assert(indices[i+j] < vertices.size());
            const auto position = [&](uint32_t index) {
                const auto& v = vertices[index]; return Vector3{v.position[0],v.position[1],v.position[2]};
            };
            const auto& v = vertices[indices[i]];
            const auto cross = Vector3::Cross(position(indices[i+1]) - position(indices[i]), position(indices[i+2]) - position(indices[i]));
            assert(cross.x * v.normal[0] + cross.y * v.normal[1] + cross.z * v.normal[2] > 0);
        }
        auto range = library.Resolve(shape, "missing-model.obj", "");
        assert(range && range->end == range->begin + 1);
        const auto revision = library.Revision();
        auto again = library.Resolve(shape, "", "");
        assert(again && again->begin == range->begin && library.Revision() == revision);
        assert(!library.Resolve(shape, "", "missing-texture.png") && library.Revision() == revision);
    }
    ModelInstance pose; assert(pose.Initialize(library.Snapshot()));
    Fixture f;
    auto entity = f.session.GetLevel()->CreateEntity("Sphere");
    const auto id = Id(entity);
    assert(entity.AddComponent<PrimitiveRenderComponent>());
    assert(f.session.GetLevel()->Update(0));
    auto emptySnapshot = RenderSceneExtractor{}.Extract(*f.session.GetLevel()->GetScene(), 1);
    assert(emptySnapshot && emptySnapshot->primitives.size() == 1 && emptySnapshot->primitives[0].nodeEnd == 0);
    const PropertyHandle handle{id,"PrimitiveRender","shape"};
    f.properties.Begin(handle);
    assert(f.properties.Preview(handle, EnumValue{static_cast<int>(PrimitiveShape::Sphere)}));
    f.properties.Commit();
    assert(entity.GetComponent<PrimitiveRenderComponent>()->shape == PrimitiveShape::Sphere);
    const auto file = std::filesystem::path("Generated/EditorTests/primitive-shapes.nxlevel");
    assert(f.session.GetLevel()->Save(file));
    Level restored; assert(restored.Load(file));
    assert(restored.FindEntity(id).GetComponent<PrimitiveRenderComponent>()->shape == PrimitiveShape::Sphere);
    assert(restored.Update(0));
    RenderScene snapshot;
    assert(RenderSceneExtractor{}.ExtractInto(*restored.GetScene(), 1.0f, snapshot));
    assert(snapshot.primitives.size() == 1 && snapshot.primitives[0].shape == PrimitiveShape::Sphere);
    assert(snapshot.primitives[0].nodeEnd != 0);
    // Clearing a mesh and saving/reloading keeps it unassigned in the new format.
    auto* primitive = restored.FindEntity(id).GetComponent<PrimitiveRenderComponent>();
    primitive->shape = PrimitiveShape::Asset;
    primitive->nodeIndex = 1;
    assert(restored.Save(file));
    Level cleared; assert(cleared.Load(file));
    assert(cleared.FindEntity(id).GetComponent<PrimitiveRenderComponent>()->meshAsset.empty());
    assert(RenderSceneExtractor{}.ExtractInto(*cleared.GetScene(), 1, snapshot));
    assert(snapshot.primitives.size() == 1 && snapshot.primitives[0].nodeEnd == 0);
    std::filesystem::remove(file);
}
