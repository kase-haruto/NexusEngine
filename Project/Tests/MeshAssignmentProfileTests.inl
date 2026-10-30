#include "Graphics/Model/ModelAssetLibrary.h"

namespace {
void MeshAssignmentCachedSnapshotTest() {
    auto cube = PrimitiveMeshBuilder::Build(PrimitiveShape::Cube); Check(cube);
    ModelAssetLibrary active("Generated", *cube);
    Check(active.Resolve(PrimitiveShape::Cube, "", ""));
    const auto snapshot = active.SharedSnapshot();
    const auto revision = active.Revision();
    auto pending = active;
    const std::array requests{ModelAssetLibrary::Request{PrimitiveShape::Asset, kStartupModelAssetReference, ""},
        ModelAssetLibrary::Request{PrimitiveShape::Cube, "", ""}, ModelAssetLibrary::Request{PrimitiveShape::Asset, "", ""}};
    pending.Retain(requests);
    Require(active.MatchesRequests(requests), "Unassigned entity must not require GPU preparation.");
    for(const auto& request : requests) Check(pending.Resolve(request.shape, request.mesh, request.texture));
    Require(pending.SharedSnapshot() == snapshot && pending.Revision() == revision, "Unassigned entity changed existing assets.");
    // 空のEntityへ既存Cubeを割り当てても、必要なGPUアセットは一切増減しない。
    const auto assigned = std::span(requests).first(2);
    Require(active.MatchesRequests(assigned), "An already loaded mesh must not require GPU preparation.");
    pending.Retain(assigned);
    for(const auto& request : assigned) Check(pending.Resolve(request.shape, request.mesh, request.texture));
    Require(pending.SharedSnapshot() == snapshot && pending.Revision() == revision, "Assigning a cached mesh changed existing assets.");
    std::cout << "PASS: adding an unassigned entity and assigning an already loaded cube both preserve the snapshot and asset revision.\n";
}
void MeshAssignmentProfileTests() {
    MeshAssignmentCachedSnapshotTest();
    // 通常のDebugエディターと同じく、GPU検証によるPSO再生成時間の増幅を除いて測る。
    performanceMeasurement = true;
    Fixture test(false);
    performanceMeasurement = false;
    const auto profile = [&](const char* label, const ModelAssetData& asset) {
        ModelInstance pose; Check(pose.Initialize(asset));
        PrimitiveRenderer renderer; renderer.SetModelInput(&asset, &pose);
        std::cout << "PROFILE BEGIN " << label << std::endl;
        Check(renderer.Initialize({test.factory, L"Project/Shaders", L"Project/Resources/Assets"}));
        const auto measured = renderer.GetInitializationProfile();
        Check(test.queue.WaitForIdle()); renderer.Shutdown();
        test.bindless.CollectGarbage(test.queue.GetCompletedFenceValue());
        std::cout << "PROFILE RESULT " << label << ": total=" << measured.totalMs << " ms; meshes=" << measured.meshes
            << "; vertices=" << measured.vertices << "; pipeline=" << measured.pipelineMs << "; geometry=" << measured.geometryMs
            << "; textures=" << measured.texturesMs << "; viewSystems=" << measured.viewSystemsMs << "; meshlets=" << measured.meshletsMs << "\n";
        return measured;
    };
    auto cube = PrimitiveMeshBuilder::Build(PrimitiveShape::Cube); Check(cube);
    const auto coldCube = profile("cube_only_cold", *cube);
    const auto warmCube = profile("cube_only_warm", *cube);
    const auto root = std::filesystem::absolute("Project/Resources/Assets");
    const auto loadStart = std::chrono::steady_clock::now();
    auto startup = ModelLoader{}.LoadGltf(root / "Models/Sponza/Sponza.gltf"); Check(startup);
    auto spheres = AppendSponzaOpticalSpheres(*startup); Check(spheres);
    Check(AppendPBRTestScene(*startup));
    std::cout << "PROFILE startup CPU load=" << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-loadStart).count() << " ms\n";
    ModelAssetLibrary active(root, *startup);
    const auto original = active.SharedSnapshot();
    auto pending = active;
    std::array requests{ModelAssetLibrary::Request{PrimitiveShape::Asset, kStartupModelAssetReference, ""},
        ModelAssetLibrary::Request{PrimitiveShape::Asset, "", ""}};
    pending.Retain(requests);
    for(const auto& request : requests) Check(pending.Resolve(request.shape, request.mesh, request.texture));
    Require(pending.SharedSnapshot() == original && pending.Revision() == active.Revision(),
        "An unassigned entity unexpectedly changed the asset snapshot.");
    std::cout << "PROFILE unassigned entity: snapshot unchanged, revision unchanged; current editor skips GPU preparation.\n";
    // 新規Entityの空参照からCubeを選択する操作と同じRetain/Resolve経路を使う。
    requests[1].shape = PrimitiveShape::Cube;
    const auto mergeStart = std::chrono::steady_clock::now();
    pending.Retain(requests);
    for(const auto& request : requests) Check(pending.Resolve(request.shape, request.mesh, request.texture));
    std::cout << "PROFILE add cube CPU merge=" << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-mergeStart).count()
        << " ms; added vertices=" << cube->meshes.front().vertexData.size()/sizeof(ModelVertex) << "\n";
    const auto scene = profile("existing_sponza_plus_cube", pending.Snapshot());
    Require(scene.meshes == startup->meshes.size()+1 && scene.vertices > warmCube.vertices,
        "The reproduction did not include the existing scene's geometry.");
    std::cout << "PROFILE SUMMARY cube_cold=" << coldCube.totalMs << " ms; cube_warm=" << warmCube.totalMs
        << " ms; existing_scene_plus_cube=" << scene.totalMs << " ms\n";
    test.Validate();
}
}
