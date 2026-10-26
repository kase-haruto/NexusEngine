// Asset switching must release unused source/variant payloads and preserve published snapshots.
void AssetMemoryTests() {
    using namespace NexusEngine;
    const auto folder = std::filesystem::absolute("Generated/EditorTests/AssetMemory");
    std::filesystem::create_directories(folder);
    const auto writeMesh = [&](const std::string& name) {
        std::ofstream file(folder / name);
        file << "v -1 -1 0\nv 0 1 0\nv 1 -1 0\nf 1 2 3\n";
    };
    auto generated = PrimitiveMeshBuilder::Build(PrimitiveShape::Cube); assert(generated);
    auto startup = std::make_shared<ModelAssetData>(std::move(*generated));
    startup->materials.front().baseColorImageData.resize(8 * 1024 * 1024);
    std::weak_ptr<const ModelAssetData> startupLifetime = startup;
    int reloads = 0;
    ModelAssetLibrary library(startup, folder, [&]() {
        ++reloads;
        return PrimitiveMeshBuilder::Build(PrimitiveShape::Cube);
    });
    auto published = library.SharedSnapshot();
    assert(published.get() == startup.get()); // No startup or editor snapshot deep copy.
    startup.reset();
    library.Retain({});
    assert(!startupLifetime.expired() && published->materials.front().baseColorImageData.size() == 8 * 1024 * 1024);
    published.reset();
    assert(startupLifetime.expired()); // The unused synthetic startup source is evictable too.
    assert(library.Model(kStartupModelAssetReference) && reloads == 1);

    std::string previousName;
    for(int iteration = 0; iteration < 64; ++iteration) {
        const auto name = "switch-" + std::to_string(iteration) + ".obj";
        writeMesh(name);
        const std::array requests{ModelAssetLibrary::Request{PrimitiveShape::Asset, name, ""}};
        auto old = library.SharedSnapshot();
        std::weak_ptr<const ModelAssetData> lifetime = old;
        const auto revision = library.Revision();
        library.Retain(requests);
        if(!previousName.empty()) assert(!library.Model(previousName)); // File is gone, so a stale source cache would mask eviction.
        auto range = library.Resolve(name, ""); assert(range && range->end - range->begin == 1);
        assert(library.Snapshot().meshes.size() == 1 && library.Snapshot().nodes.size() == 1);
        assert(library.Snapshot().materials.size() == 1 && library.Revision() > revision);
        old.reset(); assert(lifetime.expired());
        const auto stableRevision = library.Revision();
        auto current = library.SharedSnapshot();
        assert(current.get() == &library.Snapshot());
        std::filesystem::remove(folder / name);
        library.Retain(requests);
        assert(library.Resolve(name, "") && library.Model(name)); // Active sources stay available.
        assert(library.Revision() == stableRevision && library.SharedSnapshot().get() == current.get());
        previousName = name;
    }
    // A scene with multiple models must keep every active range, then compact before resolving again.
    writeMesh("a.obj"); writeMesh("b.obj");
    std::array requests{ModelAssetLibrary::Request{PrimitiveShape::Asset, "a.obj", ""},
        ModelAssetLibrary::Request{PrimitiveShape::Asset, "b.obj", ""}};
    library.Retain(requests);
    auto a = library.Resolve("a.obj", ""), b = library.Resolve("b.obj", "");
    assert(a && b && b->begin == a->end && library.Snapshot().meshes.size() == 2);
    auto original = library.SharedSnapshot();
    auto sphere = library.Resolve(PrimitiveShape::Sphere, "", ""); assert(sphere);
    assert(original->meshes.size() == 2 && library.Snapshot().meshes.size() == 3); // Published data is immutable.
    library.Retain(std::span(requests).subspan(1));
    b = library.Resolve("b.obj", "");
    assert(b && b->begin == 0 && b->materialBegin == 0 && library.Snapshot().meshes.size() == 1);
    // Texture variants are pruned even when the source mesh remains active.
    std::vector<uint8_t> bitmap(70);
    bitmap[0]='B'; bitmap[1]='M'; bitmap[2]=70; bitmap[10]=54; bitmap[14]=40;
    bitmap[18]=2; bitmap[22]=2; bitmap[26]=1; bitmap[28]=24; bitmap[34]=16;
    for(const auto* name : {"red.bmp", "blue.bmp"}) {
        std::ofstream file(folder / name, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bitmap.data()), static_cast<std::streamsize>(bitmap.size()));
    }
    std::array variants{ModelAssetLibrary::Request{PrimitiveShape::Asset, "b.obj", "red.bmp"},
        ModelAssetLibrary::Request{PrimitiveShape::Asset, "b.obj", "blue.bmp"}};
    library.Retain(variants);
    assert(library.Resolve("b.obj", "red.bmp") && library.Resolve("b.obj", "blue.bmp"));
    assert(library.Snapshot().materials.size() == 2);
    std::filesystem::remove(folder / "red.bmp"); std::filesystem::remove(folder / "blue.bmp");
    library.Retain(std::span(variants).subspan(1));
    assert(library.Resolve("b.obj", "blue.bmp") && library.Snapshot().materials.size() == 1);
    assert(!library.Texture("red.bmp") && library.Texture("blue.bmp"));
    library.Retain({});
    assert(library.Snapshot().meshes.empty() && library.Snapshot().materials.empty() && library.Snapshot().nodes.empty());
    ModelInstance emptyPose; assert(emptyPose.Initialize(library.Snapshot()));
    assert(emptyPose.GetNodeWorldTransforms().empty());
    std::filesystem::remove(folder / "a.obj"); std::filesystem::remove(folder / "b.obj");
    std::cout << "PASS: 64 asset switches retain one mesh, evict unused sources, share snapshots and release startup payloads\n";
}
