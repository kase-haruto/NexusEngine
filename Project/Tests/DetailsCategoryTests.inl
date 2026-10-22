// EditorServicesTests.cppのFixtureと同じ編集サービスを使用する統合検証。
void DetailsCategoryIntegration() {
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
    DetailsPanel panel(registry);
    ImGui::CreateContext();
    auto& io = ImGui::GetIO(); io.DisplaySize = {1000,800}; io.DeltaTime = 1.0f/60; io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    unsigned char* pixels; int width,height; io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height); io.Fonts->SetTexID(1);
    const auto frame = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({520,750});
        ImGui::Begin("Details Test"); panel.Draw(context); ImGui::End();
        ImGui::Render();
    };
    const auto child = [&](const char* suffix) -> ImGuiWindow* {
        for(auto* window : ImGui::GetCurrentContext()->Windows)
            if(window->Active && window->ParentWindow && std::string_view(window->ParentWindow->Name) == "Details Test" && std::strstr(window->Name, suffix)) return window;
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
    choose("Rendering");
    choose("Material");
    auto* page = child("PropertyPage");
    ImGui::FocusWindow(page);
    ImGui::ActivateItemByID(ImHashStr("###UseSource", 0, ImHashStr("Material", 0, page->ID)));
    frame(); frame();
    auto material = [&] { return f.session.GetLevel()->FindEntity(id).GetComponent<PrimitiveRenderComponent>()->material; };
    assert(material().enabled && material().metallic == .7f && material().baseColor == source.materials[0].baseColorFactor);
    assert(f.properties.Undo()); assert(!material().enabled); assert(!f.properties.CanUndo());
    assert(f.properties.Redo());
    // 複数Previewはカテゴリが変わっても共通の一つの履歴になる。
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
    // NameとComponent追加はMaterialページを選択中でも常にヘッダから操作できる。
    auto* header = ImGui::FindWindowByName("Details Test");
    ImGui::FocusWindow(header);
    const auto entitySeed = ImHashStr((std::to_string(id.high) + ":" + std::to_string(id.low)).c_str(), 0, header->ID);
    ImGui::ActivateItemByID(ImHashStr("###AddComponentButton", 0, entitySeed));
    frame(); frame();
    assert(ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId));
    ImGui::ClosePopupsOverWindow(header, false);
    frame();
    // 専用UIのないComponentは「その他」へ自動表示し、Reflectionで編集できる。
    TypeDescriptor custom{"CustomSettings", "component.name", [](Entity e) { return e.HasComponent<CustomComponent>(); }, {}};
    custom.properties.push_back({"value", "property.name", PropertyKind::String,
        [](Entity e) -> std::optional<PropertyValue> { if(auto* c = e.GetComponent<CustomComponent>()) return c->value; return std::nullopt; },
        [](Entity e, const PropertyValue& value) { auto* c = e.GetComponent<CustomComponent>(); auto* textValue = std::get_if<std::string>(&value);
            if(!c || !textValue) return false; c->value = *textValue; return true; }, {}});
    assert(f.types.Register(std::move(custom)));
    assert(f.session.GetSelectedEntity().AddComponent<CustomComponent>());
    frame(); choose("Other");
    assert(f.properties.Preview({id,"CustomSettings","value"}, std::string("edited"))); f.properties.Commit();
    assert(f.session.GetSelectedEntity().GetComponent<CustomComponent>()->value == "edited");
    assert(f.properties.Undo());
    bool customVisible = false;
    assert(registry.Register({"Custom", "properties.other", 800, [&](const auto&) { return customVisible; },
        [](const auto&) {}, {}, {"CustomSettings"}}));
    frame(); assert(panel.ActiveCategory() == "Other"); // 専用カテゴリが非表示ならfallbackを維持する。
    customVisible = true; frame(); assert(panel.ActiveCategory() == "Object"); choose("Custom");
    customVisible = false; frame(); assert(panel.ActiveCategory() == "Object"); choose("Other");
    assert(f.session.GetSelectedEntity().RemoveComponent<CustomComponent>());
    frame(); assert(panel.ActiveCategory() == "Object");
    // Component追加後にCamera/Lightのカテゴリが現れ、Undoで対象がなくなれば安全に戻る。
    assert(f.edits.AddComponent(*f.types.Find("Camera"))); frame(); choose("Camera");
    assert(f.properties.Undo()); frame(); assert(panel.ActiveCategory() == "Object");
    assert(f.edits.AddComponent(*f.types.Find("DirectionalLight"))); frame(); choose("Light");
    assert(f.properties.Undo()); frame(); assert(panel.ActiveCategory() == "Object"); choose("Material");
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
