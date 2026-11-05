#include <future>
#include <atomic>
// Upload snapshotを分離しながら、読み込み済みSourceをライブラリ間で共有する。
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
    assert(!startupLifetime.expired()); // Detaching does not discard an already loaded source.
    assert(library.Model(kStartupModelAssetReference) && reloads == 0);

    std::string previousName;
    for(int iteration = 0; iteration < 64; ++iteration) {
        const auto name = "switch-" + std::to_string(iteration) + ".obj";
        writeMesh(name);
        const std::array requests{ModelAssetLibrary::Request{PrimitiveShape::Asset, name, ""}};
        auto old = library.SharedSnapshot();
        std::weak_ptr<const ModelAssetData> lifetime = old;
        const auto revision = library.Revision();
        library.Retain(requests);
        if(!previousName.empty()) assert(library.Model(previousName)); // Cached models survive removal of their source file.
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
    // 遅いローダーを待つ間も、描画側はキャッシュを即時参照できる。
    std::promise<void> entered, proceed;
    auto released = proceed.get_future().share();
    auto empty = std::make_shared<ModelAssetData>();
    ModelAssetLibrary active(empty, folder, [&]() {
        entered.set_value(); released.wait();
        return PrimitiveMeshBuilder::Build(PrimitiveShape::Cube);
    });
    active.Retain({});
    auto loading = std::async(std::launch::async, [copy = active]() mutable {
        auto range = copy.Resolve(kStartupModelAssetReference, ""); assert(range);
        return copy.SharedSnapshot();
    });
    entered.get_future().wait();
    const bool isolated = active.Snapshot().meshes.empty() && !active.FindModel(kStartupModelAssetReference) &&
        !active.FindRange(PrimitiveShape::Asset, kStartupModelAssetReference, "");
    proceed.set_value();
    auto prepared = loading.get();
    assert(isolated && prepared->meshes.size() == 1 && active.Snapshot().meshes.empty());
    assert(active.FindModel(kStartupModelAssetReference)); // Worker loads are visible to the main library.
    // A thumbnail-only library copy must populate the cache used by future attachments.
    writeMesh("preview.obj");
    const ModelAssetData* previewSource = nullptr;
    {
        auto thumbnail = library;
        auto source = thumbnail.Model("preview.obj"); assert(source);
        previewSource = *source;
    }
    std::filesystem::remove(folder / "preview.obj");
    auto source = library.Model("preview.obj"); assert(source && *source == previewSource);
    library.Retain({});
    assert(library.Resolve("preview.obj", ""));
    // Concurrent requests share one in-flight load, including when its original copy is discarded.
    std::promise<void> loadEntered, finishLoad, secondStarted;
    auto finish = finishLoad.get_future().share();
    std::atomic<int> loadCount = 0;
    ModelAssetLibrary shared(std::make_shared<ModelAssetData>(), folder, [&]() {
        ++loadCount; loadEntered.set_value(); finish.wait();
        return PrimitiveMeshBuilder::Build(PrimitiveShape::Cube);
    });
    auto first = std::async(std::launch::async, [copy = shared]() mutable {
        return copy.Model(kStartupModelAssetReference);
    });
    loadEntered.get_future().wait();
    auto second = std::async(std::launch::async, [copy = shared, &secondStarted]() mutable {
        secondStarted.set_value();
        return copy.Model(kStartupModelAssetReference);
    });
    secondStarted.get_future().wait();
    assert(!shared.FindModel(kStartupModelAssetReference));
    assert(second.wait_for(std::chrono::milliseconds(20)) != std::future_status::ready);
    finishLoad.set_value();
    auto firstResult = first.get(), secondResult = second.get();
    assert(firstResult && secondResult && *firstResult == *secondResult && loadCount == 1);
    assert(shared.FindModel(kStartupModelAssetReference) == *firstResult);
    assert(!library.Model("retry.obj"));
    writeMesh("retry.obj");
    assert(library.Model("retry.obj")); // Failed loads must not poison subsequent attempts.
    std::filesystem::remove(folder / "retry.obj");
    std::filesystem::remove(folder / "a.obj"); std::filesystem::remove(folder / "b.obj");
    // Clip indices must remain local to each mesh/material variant after aggregate compaction.
    auto animated = PrimitiveMeshBuilder::Build(PrimitiveShape::Cube); assert(animated);
    AnimationClip clip; clip.duration = 1;
    NodeAnimationChannel channel; channel.nodeIndex = 0;
    channel.translations = {{0,{0,0,0},{},{}},{1,{2,0,0},{},{}}}; clip.channels.push_back(channel);
    animated->animations = {clip, clip};
    ModelAssetLibrary animationLibrary(folder, *animated);
    {
        std::ofstream file(folder / "animated.bmp", std::ios::binary);
        file.write(reinterpret_cast<const char*>(bitmap.data()), static_cast<std::streamsize>(bitmap.size()));
    }
    auto variant = animationLibrary.Resolve(kStartupModelAssetReference, "animated.bmp"); assert(variant);
    assert(variant->animationBegin == 2 && variant->animationCount == 2);
    PrimitiveRenderItem item;
    item.animation = PrimitiveRenderItem::AnimationSample{1, .5f, false}; variant->ResolveAnimation(item);
    assert(item.animation && item.animation->index == 3);
    ModelInstance animatedPose; assert(animatedPose.Initialize(animationLibrary.Snapshot()));
    assert(animatedPose.Sample(item.animation->index, item.animation->time, item.animation->loop));
    assert(animatedPose.GetNodeWorldTransforms()[variant->begin].At(3,0) == 1);
    assert(animatedPose.GetNodeWorldTransforms()[0].At(3,0) == 0); // Other variants stay in bind pose.
    item.animation->index = 2; variant->ResolveAnimation(item); assert(!item.animation);
    const std::array animatedRequests{ModelAssetLibrary::Request{PrimitiveShape::Asset,kStartupModelAssetReference,"animated.bmp"}};
    animationLibrary.Retain(animatedRequests);
    variant = animationLibrary.Resolve(kStartupModelAssetReference,"animated.bmp"); assert(variant);
    assert(variant->begin == 0 && variant->animationBegin == 0 && variant->animationCount == 2);
    std::cout << "PASS: asset reuse, source caching, retries and animation clip remapping across variants/compaction\n";
}
