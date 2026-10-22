// EditorServicesTests.cppのFixtureと同じ編集サービスを使用する統合検証。
void PropertiesPanelIntegration() {
    Fixture f;
    auto entity = f.session.GetLevel()->CreateEntity("Material entity");
    const auto id = Id(entity);
    assert(entity.AddComponent<PrimitiveRenderComponent>()); f.session.Select(id);
    PropertyDrawerRegistry drawers; drawers.RegisterDefaults();
    LocalizationService text; assert(text.Load(std::filesystem::absolute("Project/Resources/Editor/Localization")));
    EditorCommandRegistry commands;
    AssetPropertyWidget assets;
    ModelAssetData source;
    source.materials.resize(2);
    source.materials[0].name = "Surface";
    source.materials[0].baseColorFactor = {.2f,.4f,.6f,1};
    source.materials[0].metallicFactor = .7f;
    source.materials[0].roughnessFactor = .3f;
    source.materials[1].name = "Surface"; // 同名SlotでもIDはindexで区別する。
    assets.Configure(std::filesystem::absolute("Project/Resources/Assets"), {}, [&](const auto&) { return &source; }, {});
    const EditorContext context{f.session, f.session.Selection(), f.properties, f.types, drawers, text, commands, f.edits, &assets};
    PropertyCategoryRegistry registry; RegisterPropertyPages(registry);
    assert(!registry.Register({"Material", "properties.material", 0, [](const auto&) { return true; }, [](const auto&) {}}));
    int extensionDraws = 0;
    assert(registry.Register({"Extension", "properties.object", 900, [](const auto& c) { return c.session.GetSelectedEntity().IsAlive(); },
        [&](const auto&) { ++extensionDraws; }}));
    PropertiesPanel panel(registry);
    DetailsPanel details;
    ImGui::CreateContext();
    auto& io = ImGui::GetIO(); io.DisplaySize = {1000,800}; io.DeltaTime = 1.0f/60; io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    unsigned char* pixels; int width,height; io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height); io.Fonts->SetTexID(1);
    const auto frame = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({520,750});
        ImGui::Begin("Properties Test"); panel.Draw(context); ImGui::End();
        ImGui::SetNextWindowPos({530,0}); ImGui::SetNextWindowSize({460,750});
        ImGui::Begin("Details Test"); details.Draw(context); ImGui::End();
        ImGui::Render();
    };
    const auto child = [&](const char* suffix) -> ImGuiWindow* {
        for(auto* window : ImGui::GetCurrentContext()->Windows)
            if(window->Active && window->ParentWindow && std::string_view(window->ParentWindow->Name) == "Properties Test" && std::strstr(window->Name, suffix)) return window;
        assert(false); return nullptr;
    };
    const auto choose = [&](const char* category) {
        auto* navigation = child("CategoryNavigation");
        ImGui::FocusWindow(navigation);
        const auto categorySeed = ImHashStr(category, 0, navigation->ID);
        ImGui::ActivateItemByID(ImHashStr("##category", 0, categorySeed));
        frame(); frame();
        assert(panel.ActiveCategory() == category);
    };
    frame(); frame(); assert(panel.ActiveCategory() == "Object");
    choose("Material");
    auto* page = child("PropertyPage");
    ImGui::FocusWindow(page);
    ImGui::ActivateItemByID(ImHashStr("###UseSource", 0, ImHashStr("Material", 0, page->ID)));
    frame(); frame();
    auto material = [&] { return f.session.GetLevel()->FindEntity(id).GetComponent<PrimitiveRenderComponent>()->material; };
    assert(material().enabled && material().metallic == .7f && material().baseColor == source.materials[0].baseColorFactor);
    assert(f.properties.Undo()); assert(!material().enabled); assert(!f.properties.CanUndo());
    assert(f.properties.Redo());
    // 複数Previewは一つの履歴となり、別Panelからも同じ値を読む。
    const PropertyHandle roughness{id,"PrimitiveRender","materialRoughness"};
    assert(f.properties.Preview(roughness,.45f)); assert(f.properties.Preview(roughness,.65f)); f.properties.Commit();
    assert(!f.properties.Preview(roughness,-1.0f)); f.properties.Cancel();
    assert(material().roughness == .65f);
    assert(f.properties.Undo()); assert(material().roughness == .3f);
    assert(f.properties.Redo());
    const std::vector<PropertyHandle> extras{{id,"PrimitiveRender","materialNormalScale"},
        {id,"PrimitiveRender","materialEmissive"}, {id,"PrimitiveRender","materialOcclusion"}};
    assert(f.properties.PreviewValues(extras, std::vector<PropertyValue>{2.0f, std::array<float,3>{.3f,.5f,.7f}, .4f}));
    f.properties.Commit();
    assert(f.session.GetLevel()->Update(0));
    const auto extracted = RenderSceneExtractor{}.Extract(*f.session.GetLevel()->GetScene(),1);
    assert(extracted && extracted->primitives.size() == 1 && extracted->primitives[0].material.roughness == .65f);
    // Raster/DXRが共有する適用処理とSource Slotのoffsetを検証する。
    auto override = material();
    assert(override.AppliesTo(12,12) && !override.AppliesTo(13,12) && !override.AppliesTo(11,12));
    std::array<float,4> color{}, pbr{}, emission{}; override.Apply(color,pbr,emission);
    assert(color == override.baseColor && pbr[0] == .7f && pbr[1] == .65f && pbr[2] == 2 && emission[3] == .4f);
    const auto path = std::filesystem::absolute("Generated/EditorTests/MaterialOverride.level");
    std::filesystem::create_directories(path.parent_path()); assert(f.documents.Save(path));
    assert(!f.properties.IsDirty());
    Level loaded("Loaded"); assert(loaded.Load(path));
    const auto* restored = loaded.FindEntity(id).GetComponent<PrimitiveRenderComponent>();
    assert(restored && restored->material.enabled && restored->material.roughness == .65f && restored->material.metallic == .7f);
    assert(restored->material.normalScale == 2 && restored->material.emissive == override.emissive && restored->material.occlusionStrength == .4f);
    assert(source.materials[0].roughnessFactor == .3f && source.materials[0].normalScale == 1);
    // v7の既存Levelを読む場合はSource材質のままで、Overrideを有効化しない。
    const auto legacyPath = path.parent_path() / "MaterialLegacy.level";
    {
        std::ofstream legacy(legacyPath);
        legacy << "NEXUS_LEVEL 7\nLEVEL \"legacy\"\nLIGHTING 0\nENVIRONMENT \"\" 1 0\n"
            "ENTITY 1 1 \"mesh\"\nTRANSFORM 0 0 0 0 0 0 1 1 1 1\nPARENT 0 0\nCAMERA 0\n"
            "PRIMITIVE 1 1 1 1 1 1 4294967295 1 \"\" \"\"\nDIRECTIONAL_LIGHT 0\nEND_ENTITY\nEND_LEVEL\n";
    }
    Level legacy; assert(legacy.Load(legacyPath));
    assert(!legacy.FindEntity({1,1}).GetComponent<PrimitiveRenderComponent>()->material.enabled);
    loaded.FindEntity(id).GetComponent<PrimitiveRenderComponent>()->material.metallic = std::numeric_limits<float>::infinity();
    assert(!loaded.Save(path.parent_path() / "InvalidMaterial.level"));
    text.SetLanguage("ja"); frame(); assert(panel.ActiveCategory() == "Material");
    choose("Extension"); assert(extensionDraws > 0);
    choose("Material");
    assert(f.edits.DeleteSelection()); frame(); assert(panel.ActiveCategory().empty());
    assert(f.properties.Undo()); frame(); assert(panel.ActiveCategory() == "Object");
    choose("Material");
    f.session.Selection().Clear(); frame(); assert(panel.ActiveCategory().empty());
    f.session.Select(id); frame();
    assert(f.session.GetLevel()->DestroyEntity(f.session.GetSelectedEntity()));
    frame(); assert(panel.ActiveCategory().empty()); // 選択IDが残っていても破棄済みEntityを参照しない。
    assert(f.session.Create()); f.properties.Reset(f.session.GetLevel());
    frame(); assert(panel.ActiveCategory().empty());
    ImGui::DestroyContext();
}
