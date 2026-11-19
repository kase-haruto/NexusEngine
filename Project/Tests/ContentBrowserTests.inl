#include "Editor/Services/ContentBrowserState.h"
#include "Editor/Services/ContentBrowserActions.h"
#include "Editor/Services/ContentFileTransaction.h"
#include "Editor/Gui/Icons/EditorIcons.h"
#include "Platform/FileSystem/AssetIdentity.h"
#include <thread>

void ContentBrowserServiceTests() {
    const auto root = std::filesystem::absolute("Generated/ContentBrowserTests") /
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(root / "Characters");
    std::filesystem::create_directories(root / "Destination");
    std::ofstream(root / "Dynamic.custom") << "custom";
    std::ofstream(root / "Player.png") << "texture fixture";
    std::ofstream(root / "Enemy.cs") << "class Enemy {}";
    AssetCatalog catalog(root);
    const auto initial = catalog.Snapshot(root);
    const auto scans = catalog.ScanCount();
    std::ofstream(root / "External.curve") << "external";
    // 旧TTLを超えても、外部追加を勝手に走査しない。
    std::this_thread::sleep_for(std::chrono::milliseconds(2100));
    assert(catalog.Snapshot(root) == initial && catalog.ScanCount() == scans);
    // 128folderを超えた際のcache退役も、表示中の再走査を発生させない。
    for (int i = 0; i < 130; ++i) {
        const auto folder = root / ("Folder" + std::to_string(i));
        std::filesystem::create_directory(folder);
        catalog.Snapshot(folder);
    }
    const auto afterFolders = catalog.ScanCount();
    assert(catalog.Snapshot(root) == initial && catalog.ScanCount() == afterFolders);
    catalog.Refresh();
    const auto refreshed = catalog.Snapshot(root);
    assert(refreshed != initial);
    assert(std::ranges::any_of(*refreshed, [](const auto &entry) {
        return entry.name == "External.curve";
    }));
    assert(catalog.Types().Register({"Custom", "asset.custom", "custom_icon", {".custom"}}));
    assert(catalog.Types().Classify(root / "X.CUSTOM", false).icon == "custom_icon");
    const auto reclassified = catalog.Snapshot(root);
    const auto dynamicType = std::ranges::find(*reclassified, std::string("Dynamic.custom"), &AssetEntry::name);
    assert(dynamicType != reclassified->end() && dynamicType->typeKey == "asset.custom");
    assert(!catalog.Types().Register({"Custom", "asset.other", "other", {}}));
    const auto query = ContentSearchParser::Parse("type:Texture ext:.PNG path:Player");
    const auto texture = std::ranges::find(*refreshed, std::string("Player.png"), &AssetEntry::name);
    assert(texture != refreshed->end() && ContentSearchParser::Matches(query, *texture, catalog.Types()));
    assert(ContentSearchParser::Parse("size:>4MB").error.empty());
    assert(!ContentSearchParser::Parse(R"(type:"Texture)").error.empty());
    ContentBrowserState state;
    state.filters.insert("asset.texture");
    assert(state.Visible(*refreshed, catalog.Types()).size() == 1);
    state.filters.clear();
    const auto visible = state.Visible(*refreshed, catalog.Types());
    state.Select(visible, 0, false, false);
    state.Select(visible, 2, false, true);
    assert(state.selection.size() == 3);
    state.Select(visible, 1, true, false);
    assert(state.selection.size() == 2);
    state.history.Visit({0, root});
    state.history.Visit({0, root / "Characters"});
    assert(state.history.Back()->path == root && state.history.Forward()->path == root / "Characters");
    state.history.Back();
    state.history.Visit({1, root / "Destination"});
    assert(!state.history.CanForward());
    assert(ContentBrowserActions::CreateFolder(catalog, root, "New Folder"));
    assert(!ContentBrowserActions::CreateFolder(catalog, root, "../Escape"));
    EditorHistory history;
    auto revision = std::make_shared<size_t>(0);
    const auto id = catalog.StableReference(root / "Player.png");
    assert(id);
    const std::vector<std::filesystem::path> selection{root / "Player.png", root / "External.curve"};
    assert(ContentFileTransaction::Execute(catalog, selection, ContentFileOperation::Move, root / "Destination",
                                           history, revision));
    assert(!std::filesystem::exists(root / "Player.png"));
    const auto movedPath = ResolveAssetReference(root, *id);
    assert(movedPath && *movedPath == root / "Destination/Player.png");
    assert(history.Undo() && std::filesystem::exists(root / "Player.png"));
    assert(history.Redo() && std::filesystem::exists(root / "Destination/Player.png"));
    assert(history.Undo());
    assert(ContentFileTransaction::Execute(catalog, selection, ContentFileOperation::Duplicate, {}, history, revision));
    assert(std::filesystem::exists(root / "Player_Copy1.png"));
    const auto copyId = catalog.StableReference(root / "Player_Copy1.png");
    assert(copyId && *copyId != *id);
    assert(history.Undo() && !std::filesystem::exists(root / "Player_Copy1.png"));
    assert(history.Redo());
    const auto restoredCopyId = catalog.StableReference(root / "Player_Copy1.png");
    assert(restoredCopyId && *restoredCopyId == *copyId);
    assert(ContentFileTransaction::Execute(catalog, selection, ContentFileOperation::Archive, {}, history, revision));
    assert(!std::filesystem::exists(root / "Player.png"));
    assert(ResolveAssetReference(root, *id));
    assert(history.Undo() && std::filesystem::exists(root / "Player.png"));
    const std::vector<std::filesystem::path> source{root / "Enemy.cs"};
    assert(!ContentFileTransaction::Execute(catalog, source, ContentFileOperation::Archive, {}, history, revision));
    std::ofstream(root / "Destination/Player.png") << "do not replace";
    assert(!ContentFileTransaction::Execute(catalog, selection, ContentFileOperation::Move, root / "Destination",
                                            history, revision));
    assert(std::filesystem::exists(root / "External.curve"));
    std::ofstream(root / "Scene.gltf") << R"({"buffers":[{"uri":"Scene.bin"}]})";
    const std::vector<std::filesystem::path> model{root / "Scene.gltf"};
    assert(!ContentFileTransaction::Execute(catalog, model, ContentFileOperation::Move, root / "Destination", history,
                                            revision));
    assert(ContentFileTransaction::Rename(catalog, root / "Scene.gltf", "SceneRenamed.gltf", history, revision));
    assert(history.Undo() && std::filesystem::exists(root / "Scene.gltf"));
    std::filesystem::create_directory(root / "Rollback");
    {
        std::ifstream locked(root / "External.curve");
        assert(locked);
        const auto result = ContentFileTransaction::Execute(catalog, selection, ContentFileOperation::Move,
                                                            root / "Rollback", history, revision);
        assert(!result);
        assert(std::filesystem::exists(root / "Player.png") && std::filesystem::exists(root / "External.curve"));
        assert(!std::filesystem::exists(root / "Rollback/Player.png"));
    }

    std::cout << "PASS: Content Browser no-polling cache, registry/search/history/multi-selection, safe folder "
                 "creation, UUID batch move/duplicate/archive and shared Undo/Redo.\n";
}
void ContentBrowserIconTests() {
    ImGui::CreateContext();
    assert(UI::EditorIconDefinitions().size() == 56);
    UI::SetEditorIcon("edit.undo", ImTextureRef{static_cast<ImTextureID>(123)});
    assert(UI::ResolveEditorIcon("back") && UI::ResolveEditorIcon("back")->GetTexID() == 123);
    UI::SetEditorIcon("back", ImTextureRef{static_cast<ImTextureID>(124)});
    assert(UI::ResolveEditorIcon("back")->GetTexID() == 124);
    UI::SetEditorIcon("asset.folder", ImTextureRef{static_cast<ImTextureID>(125)});
    UI::SetEditorIcon("folder", ImTextureRef{static_cast<ImTextureID>(126)});
    assert(UI::ResolveEditorIcon("folder")->GetTexID() == 125);
    assert(!UI::ResolveEditorIcon("unplaced_icon"));
    UI::ClearEditorIcons();
    ImGui::DestroyContext();
}

void ContentBrowserViewIntegration() {
    wchar_t *previous = nullptr;
    size_t length = 0;
    _wdupenv_s(&previous, &length, L"LOCALAPPDATA");
    const auto root = std::filesystem::absolute("Generated/ContentBrowserViewTests") /
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(root / "Assets/Sub");
    std::ofstream(root / "Assets/Texture.png") << "fixture";
    std::ofstream(root / "Assets/Readme.txt") << "fixture";
    _wputenv_s(L"LOCALAPPDATA", (root / "User").c_str());
    EditorSettingsStore settings;
    settings.Load(root);
    ContentBrowserState preference;
    preference.view = ContentViewMode::List;
    preference.filters.insert("asset.texture");
    preference.hiddenTypes.insert("asset.csharp");
    preference.ToggleFavorite(root / "Assets/Sub");
    assert(preference.Save(&settings, "Persistence"));
    EditorSettingsStore reloaded;
    reloaded.Load(root);
    ContentBrowserState restored;
    restored.Load(&reloaded, "Persistence");
    assert(restored.filters == preference.filters && restored.hiddenTypes == preference.hiddenTypes &&
           restored.favorites == preference.favorites);
    Fixture fixture;
    AssetCatalog catalog(root / "Assets");
    ContentBrowserPanel browser(catalog, {}, nullptr, nullptr, &settings);
    LocalizationService text;
    assert(text.Load(std::filesystem::absolute("Project/Resources/Editor/Localization")));
    PropertyDrawerRegistry drawers;
    drawers.RegisterDefaults();
    EditorCommandRegistry commands;
    const EditorContext context{
        fixture.session, fixture.session.Selection(), fixture.properties, fixture.types, drawers, text, commands,
        fixture.edits};
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.DisplaySize = {1200, 800};
    io.DeltaTime = .1f;
    io.IniFilename = nullptr;
    unsigned char *pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    io.Fonts->SetTexID(1);
    const auto frame = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowSize({1100, 700});
        ImGui::Begin("Content Browser View Test");
        browser.Draw(context);
        ImGui::End();
        ImGui::Render();
    };
    frame();
    frame();
    const auto activate = [&](const char *id) {
        auto *window = ImGui::FindWindowByName("Content Browser View Test");
        assert(window);
        ImGui::FocusWindow(window);
        ImGui::ActivateItemByID(ImHashStr(id, 0, window->ID));
        frame();
        frame();
    };
    activate("view_grid");
    ContentBrowserState grid;
    grid.Load(&settings, AssetCatalog::Utf8(catalog.Root()));
    assert(grid.view == ContentViewMode::Grid);
    const auto scans = catalog.ScanCount();
    for (int i = 0; i < 100; ++i) {
        frame();
    }
    assert(catalog.ScanCount() == scans);
    activate("view_details");
    ContentBrowserState details;
    details.Load(&settings, AssetCatalog::Utf8(catalog.Root()));
    assert(details.view == ContentViewMode::Details);
    UI::ClearEditorIcons();
    ImGui::DestroyContext();
    _wputenv_s(L"LOCALAPPDATA", previous ? previous : L"");
    std::free(previous);
    std::cout << "PASS: Content Browser actual view buttons, grid/details rendering, preferences persistence and 100 "
                 "idle frames without directory scans.\n";
}
