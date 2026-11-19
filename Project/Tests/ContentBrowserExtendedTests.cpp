#include "Editor/Services/ContentBrowserServices.h"
#include "Editor/Services/ContentImportService.h"
#include "Editor/Services/ContentMutationService.h"
#include "Editor/Services/ContentDragPayload.h"
#include "Editor/Services/ContentBrowserState.h"
#include "Editor/Services/SourceFileActions.h"
#include "Editor/Services/LocalizationService.h"
#include "Platform/FileSystem/AtomicFile.h"
#include "Platform/FileSystem/AssetIdentity.h"
#include <cassert>
#include <fstream>
#include <thread>
#include <iostream>
#include <cstring>

namespace NexusEngine::Tests {
namespace {
void Put(const std::filesystem::path &file, std::string_view bytes) {
    std::filesystem::create_directories(file.parent_path());
    assert(WriteFileAtomically(file, std::string(bytes)));
}
template <class Predicate> bool Until(Predicate condition) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}
} // namespace
void RunContentBrowserExtendedTests() {
    LocalizationService locale;
    assert(locale.Load(std::filesystem::absolute("Project/Resources/Editor/Localization")));
    locale.SetLanguage("ja");
    assert(locale.Text("menu.file") != "menu.file" && locale.Text("cb.import") != "cb.import");
    const auto uuid = CreateUuid();
    assert(uuid);
    const auto root = std::filesystem::absolute("Generated/ContentBrowserPhase2/Tests") / *uuid;
    const auto assets = root / "Assets", external = root / "External";
    std::filesystem::create_directories(assets);
    std::filesystem::create_directories(external);
    {
        DirectoryChangeWatcher watcher;
        assert(watcher.Start(assets));
        const auto begin = std::chrono::steady_clock::now();
        for (size_t index = 0; index < 10000; ++index) {
            const auto event = watcher.Poll();
            assert(event && event->paths.empty() && !event->overflow);
        }
        const auto idle = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
        assert(watcher.CompletionCount() == 0);
        Put(assets / "OS" / "New.wav", "first");
        assert(Until([&] {
            const auto event = watcher.Poll();
            return event && !event->paths.empty();
        }));
        watcher.Stop();
        std::cout << "Content watcher idle 10000 polls: " << idle << " ms; zero directory scans\n";
    }
    {
        AssetCatalog catalog(assets);
        auto snapshot = catalog.Snapshot(assets);
        catalog.SetAutomaticRefresh(true);
        assert(catalog.AutomaticRefresh());
        snapshot = catalog.Snapshot(assets);
        const auto scans = catalog.ScanCount();
        for (size_t index = 0; index < 1000; ++index) {
            assert(catalog.Snapshot(assets) == snapshot);
        }
        assert(catalog.ScanCount() == scans);
        Put(assets / "Watched.wav", "one");
        assert(Until([&] {
            return catalog.Snapshot(assets) != snapshot;
        }));
        assert(catalog.ScanCount() == scans + 1);
        snapshot = catalog.Snapshot(assets);
        Put(assets / "Build" / "Output.cpp", "ignored");
        std::this_thread::sleep_for(std::chrono::milliseconds(350));
        assert(catalog.Snapshot(assets) == snapshot);
        catalog.SetAutomaticRefresh(false);
    }
    {
        AssetCatalog catalog(assets);
        ContentBrowserServices services;
        services.AddRoot("Content", "Content", "content", catalog);
        const auto snapshot = catalog.Snapshot(assets);
        services.Poll();
        const auto initial = catalog.Snapshot(assets);
        ++*services.fileRevision;
        services.Poll();
        assert(catalog.Snapshot(assets) != initial && !catalog.AutomaticRefresh());
        static_cast<void>(snapshot);
    }
    {
        EditorSettingsStore settings;
        wchar_t *previous = nullptr;
        size_t length = 0;
        _wdupenv_s(&previous, &length, L"LOCALAPPDATA");
        _wputenv_s(L"LOCALAPPDATA", (root / "User").c_str());
        settings.Load(root);
        _wputenv_s(L"LOCALAPPDATA", previous ? previous : L"");
        std::free(previous);
        ContentLibrary first, second;
        first.Initialize(&settings, "Project", root / "Config");
        second.Initialize(&settings, "Project", root / "Config");
        const auto shared = first.Create("Team", true);
        assert(shared);
        const auto sharedHash = Sha256File(root / "Config/ContentCollections.json");
        assert(sharedHash);
        assert(!second.Create("Stale", true));
        const auto local = second.Create("Local", false);
        assert(local);
        assert(*Sha256File(root / "Config/ContentCollections.json") == *sharedHash);
        assert(second.Reload() && second.Find(*shared) && second.Find(*local));
        const ContentReference ref{"Content", "Folder/Asset.wav"};
        assert(second.Add(*shared, std::span(&ref, 1)));
        assert(second.Opened(ref));
        assert(second.SaveSearch({"Textures", "size:>4MB type:Texture", true, {{"Content", "."}}, {"asset.texture"}}));
        ContentLibrary restored;
        restored.Initialize(&settings, "Project", root / "Config");
        assert(restored.Recent().size() == 1 && restored.Searches().size() == 1 &&
               restored.Searches()[0].scopes.size() == 1);
    }
    {
        Put(assets / "Meshes/Scene.gltf",
            R"({"buffers":[{"uri":"mesh.bin"}],"accessors":[{"count":4},{"count":9}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}]})");
        Put(assets / "Meshes/mesh.bin", "binary geometry");
        Put(assets / "Uses.json", R"({"model":"Meshes/Scene.gltf"})");
        const auto metadata = ContentMetadataProvider::Read(assets / "Meshes/Scene.gltf", assets);
        assert(metadata && metadata->triangles == 3 && metadata->dependencies.size() == 1 &&
               !metadata->dependencies[0].missing);
        const auto oldId = EnsureAssetIdentity(assets, assets / "Meshes/Scene.gltf");
        assert(oldId);
        ContentMutationService move;
        assert(move.Start(assets, {{assets / "Meshes", assets / "Moved"}}));
        assert(Until([&] {
            move.Poll();
            return !move.Busy();
        }));
        assert(move.Plan());
        AssetDocumentRegistry documents;
        EditorHistory history;
        auto revision = std::make_shared<size_t>(0);
        assert(move.Publish(documents, history, revision));
        assert(ContentMetadataProvider::Text(assets / "Uses.json")->find("Moved/Scene.gltf") != std::string::npos);
        const auto resolved = ResolveAssetReference(assets, *oldId);
        assert(resolved && *resolved == assets / "Moved/Scene.gltf");
        assert(history.Undo() && std::filesystem::exists(assets / "Meshes/Scene.gltf"));
        assert(history.Redo() && std::filesystem::exists(assets / "Moved/Scene.gltf"));
        assert(!ContentMutationService::Prepare(assets,
                                                std::vector<std::pair<std::filesystem::path, std::filesystem::path>>{
                                                    {assets / "Moved", assets / "Moved/Child"}}));
    }
    {
        Put(external / "Sound.wav", "version one");
        Put(external / "Identity.NXMETA", "internal");
        assert(!ContentImportService::Prepare(std::vector<std::filesystem::path>{external / "Identity.NXMETA"}, assets,
                                              assets, ContentImportOptions{}));
        AssetCatalog catalog(assets);
        AssetDocumentRegistry documents;
        EditorHistory history;
        auto revision = std::make_shared<size_t>(0);
        ContentImportService service;
        ContentImportOptions options;
        options.dependencies = false;
        assert(service.Start({external / "Sound.wav"}, assets, assets, options));
        assert(Until([&] {
            service.Poll();
            return !service.Busy();
        }));
        assert(service.Plan() && service.Publish(catalog, documents, history, revision));
        const auto identity = EnsureAssetIdentity(assets, assets / "Sound.wav");
        assert(identity);
        assert(history.Undo() && !std::filesystem::exists(assets / "Sound.wav"));
        assert(history.Redo() && *EnsureAssetIdentity(assets, assets / "Sound.wav") == *identity);
        Put(external / "Sound.wav", "version two");
        options.replace = true;
        assert(service.Start({external / "Sound.wav"}, assets, assets, options));
        assert(Until([&] {
            service.Poll();
            return !service.Busy();
        }));
        bool dirty = true;
        assert(documents.Register({"sound", "Sound",
                                   [&] {
                                       return assets / "Sound.wav";
                                   },
                                   [&] {
                                       return dirty;
                                   },
                                   [&]() -> Result<void> {
                                       dirty = false;
                                       return {};
                                   }}));
        assert(!service.Publish(catalog, documents, history, revision));
        assert(documents.SaveAll() && !dirty);
        assert(service.Publish(catalog, documents, history, revision));
        assert(*EnsureAssetIdentity(assets, assets / "Sound.wav") == *identity);
        assert(history.Undo() && *ContentMetadataProvider::Text(assets / "Sound.wav") == "version one");
        Put(assets / "Sound.wav", "external edit");
        assert(!history.Redo() && *ContentMetadataProvider::Text(assets / "Sound.wav") == "external edit");
    }
    {
        const std::vector<std::filesystem::path> files{assets / "Sound.wav", assets / "Moved/Scene.gltf"};
        const auto encoded = ContentDragPayload::Encode(files);
        assert(encoded);
        const auto decoded = ContentDragPayload::Decode(encoded->data(), encoded->size());
        assert(decoded && *decoded == files);
        assert(!ContentDragPayload::Decode("{}", 2));
        assert(!ContentDragPayload::Decode(encoded->data(), encoded->size() - 1));
        const ContentItemRegistry types;
        AssetEntry entry{assets / "large.png", "large.png",
                         "asset.texture",      false,
                         8 * 1024 * 1024,      std::filesystem::file_time_type::clock::now()};
        assert(ContentSearchParser::Matches(ContentSearchParser::Parse("size:>4MB modified:today"), entry, types));
        assert(ContentSearchParser::Parse("modified:2026-11-19").error.empty());
        assert(!ContentSearchParser::Parse("modified:2026-02-31").error.empty());
    }
    {
        Put(assets / "Flat.gltf", R"({"buffers":[{"uri":"Buffers/flat.bin"}],"meshes":[]})");
        Put(assets / "Buffers/flat.bin", "geometry");
        ContentMutationService move;
        assert(move.Start(assets, {{assets / "Flat.gltf", assets / "Nested/Flat.gltf"}}));
        assert(Until([&] {
            move.Poll();
            return !move.Busy();
        }));
        assert(move.Plan());
        AssetDocumentRegistry documents;
        EditorHistory history;
        assert(move.Publish(documents, history, std::make_shared<size_t>(0)));
        const auto metadata = ContentMetadataProvider::Read(assets / "Nested/Flat.gltf", assets);
        assert(metadata && metadata->dependencies.size() == 1 && !metadata->dependencies[0].missing);
        assert(metadata->dependencies[0].reference == "../Buffers/flat.bin");
        assert(history.Undo());
    }
    {
        std::string json =
            R"({"asset":{"version":"2.0"},"buffers":[{"uri":"Buffers/flat.bin","byteLength":8}],"meshes":[]})";
        while (json.size() % 4) {
            json += ' ';
        }
        const uint32_t header[]{0x46546C67, 2, static_cast<uint32_t>(20 + json.size()),
                                static_cast<uint32_t>(json.size()), 0x4E4F534A};
        std::string glb(sizeof(header), '\0');
        std::memcpy(glb.data(), header, sizeof(header));
        glb += json;
        Put(assets / "Upper.GLB", glb);
        ContentMutationService move;
        assert(move.Start(assets, {{assets / "Upper.GLB", assets / "Nested/Upper.GLB"}}));
        assert(Until([&] {
            move.Poll();
            return !move.Busy();
        }));
        AssetDocumentRegistry documents;
        EditorHistory history;
        assert(move.Plan() && move.Publish(documents, history, std::make_shared<size_t>(0)));
        const auto metadata = ContentMetadataProvider::Read(assets / "Nested/Upper.GLB", assets);
        assert(metadata && metadata->dependencies.size() == 1 && !metadata->dependencies[0].missing);
        assert(metadata->dependencies[0].reference == "../Buffers/flat.bin");
        assert(history.Undo() && *ContentMetadataProvider::Text(assets / "Upper.GLB") == glb);
    }
    {
        const auto source = root / "Source";
        Put(source / "Native/Foo.h", "// class Wrong {}\nnamespace Gameplay { class Foo {}; }\n");
        Put(source / "Native/Foo.cpp", "#include \"Foo.h\"\n");
        const auto metadata = ContentMetadataProvider::Read(source / "Native/Foo.h", source);
        assert(metadata && metadata->className == "Foo" && metadata->nameSpace == "Gameplay");
        TypeRegistry types;
        TypeDescriptor type;
        type.id = "ManagedScript:Mod.behaviours.Foo";
        type.labelKey = "Foo";
        type.contains = [](Entity) {
            return false;
        };
        type.add = [](Entity) {
            return true;
        };
        type.sourceFiles = {"Native/Foo.cpp"};
        assert(types.Register(std::move(type)));
        ContentMutationService move;
        assert(move.Start(source, {{source / "Native/Foo.cpp", source / "Native/Sub/Foo.cpp"},
                                   {source / "Native/Foo.h", source / "Native/Sub/Foo.h"}}));
        assert(Until([&] {
            move.Poll();
            return !move.Busy();
        }));
        AssetDocumentRegistry documents;
        EditorHistory history;
        assert(move.Plan() && move.Publish(documents, history, std::make_shared<size_t>(0)));
        assert(SourceFileActions::TypesFor(source, source / "Native/Sub/Foo.cpp", types).size() == 1);
        Put(assets / "SourceUse.level", R"({"type":"Mod.behaviours.Foo"})");
        AssetCatalog catalog(assets), sources(source);
        ContentBrowserServices services;
        services.AddRoot("Content", "Content", "content", catalog);
        services.AddRoot("Source", "Source", "source", sources, false, true);
        services.UpdateSourceTypes(types, source);
        services.dependencies.Request(services.GraphRoots());
        assert(Until([&] {
            services.dependencies.Poll();
            return !services.dependencies.Busy();
        }));
        assert(services.dependencies.Snapshot()->references.contains(source / "Native/Sub/Foo.cpp"));
    }
    std::cout << "PASS: Content library conflict handling, OS notifications, metadata, import/reimport, dependency "
                 "moves, undo and payload validation\n";
}
} // namespace NexusEngine::Tests
