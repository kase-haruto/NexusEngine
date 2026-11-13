namespace {
void ViewportPreviewTests() {
    Fixture f;
    auto* level = f.session.GetLevel();
    auto first = level->CreateEntity("Camera A"), second = level->CreateEntity("Camera B"), object = level->CreateEntity("Object");
    static_cast<void>(first.AddComponent<CameraComponent>()); static_cast<void>(second.AddComponent<CameraComponent>());
    const auto a = Id(first), b = Id(second), other = Id(object);
    first.GetComponent<TransformComponent>()->translation = {0, 0, -5};
    second.GetComponent<TransformComponent>()->translation = {4, 1, -8};
    assert(level->Update(0));
    f.session.MarkSaved();
    assert(f.session.SetCameraPreview(a));
    f.session.Select(other);
    assert(f.session.GetCameraPreview() == a && f.session.GetSelection() == other && !f.session.IsDirty());
    assert(!f.session.SetCameraPreview(other) && f.session.GetCameraPreview() == a);
    const auto cameraA = f.session.ResolveCameraPreview(16.0f/9);
    assert(cameraA);
    assert(f.session.SetCameraPreview(b));
    assert(f.session.ResolveCameraPreview(16.0f/9)->sourceObject != cameraA->sourceObject);
    // ゲーム用の有効・主カメラ設定に関係なく、明示指定された視点を確認できる。
    second.GetComponent<CameraComponent>()->enabled = false;
    second.GetComponent<CameraComponent>()->primary = false;
    assert(f.session.ResolveCameraPreview(16.0f/9));
    assert(!f.session.ResolveCameraPreview(0));
    const float nearClip = second.GetComponent<CameraComponent>()->nearClip;
    second.GetComponent<CameraComponent>()->nearClip = -1;
    assert(!f.session.ResolveCameraPreview(16.0f/9) && f.session.GetCameraPreview() == b);
    second.GetComponent<CameraComponent>()->nearClip = nearClip;
    assert(f.session.StartPlay());
    f.session.GetPlayLevel()->FindEntity(b).GetComponent<TransformComponent>()->translation = {20, 0, -5};
    assert(f.session.GetPlayLevel()->Update(0));
    assert(f.session.ResolveCameraPreview(16.0f/9)->viewMatrix.elements != TryInverse(second.GetComponent<TransformComponent>()->worldMatrix)->elements);
    f.session.StopPlay();
    assert(f.session.GetCameraPreview() == b);
    assert(level->DestroyEntity(second));
    assert(!f.session.ResolveCameraPreview(16.0f/9) && !f.session.GetCameraPreview().IsValid());
    assert(f.session.SetCameraPreview(a));
    first.RemoveComponent<CameraComponent>();
    assert(!f.session.ResolveCameraPreview(16.0f/9) && !f.session.GetCameraPreview().IsValid());
    static_cast<void>(first.AddComponent<CameraComponent>());
    auto third = level->CreateEntity("Camera C"); static_cast<void>(third.AddComponent<CameraComponent>());
    assert(level->Update(0));
    RenderScene scene;
    // 複数の主カメラがあるLevelも編集でき、ゲーム用の抽出は従来どおり競合を検出する。
    RenderSceneExtractor extractor;
    assert(extractor.ExtractInto(*level->GetScene(), 1, scene, false));
    assert(!scene.camera && !extractor.ExtractInto(*level->GetScene(), 1, scene));
    f.session.Diagnostics().Set("asset", "missing", true);
    f.session.Diagnostics().Set("asset", "updated", true);
    assert(f.session.Diagnostics().Items().size() == 1);
    f.session.Diagnostics().Resolve("asset"); assert(f.session.Diagnostics().Items().empty());
    assert(f.session.SetCameraPreview(a));
    assert(f.session.Create());
    assert(!f.session.GetCameraPreview().IsValid());
}

void ViewportPreviewOverlayTests() {
    Fixture f;
    auto* level = f.session.GetLevel();
    auto camera = level->CreateEntity("Camera A"), object = level->CreateEntity("Object");
    static_cast<void>(camera.AddComponent<CameraComponent>());
    camera.GetComponent<TransformComponent>()->translation = {0,0,-5};
    const auto cameraId = Id(camera), other = Id(object);
    assert(level->Update(0));
    assert(f.session.SetCameraPreview(cameraId)); f.session.Select(other);
    ViewportTools tools(f.session, f.properties);
    ViewportSession viewport;
    class Textures final : public UI::ITextureProvider {
    public:
        std::optional<UI::ResolvedTexture> Resolve(UI::TextureHandle token) const noexcept override {
            return UI::ResolvedTexture{token.value == 1 ? 11u : 12u, 640, 360};
        }
    } textures;
    uint32_t renderWidth = 0, renderHeight = 0;
    assert(viewport.AttachLocalPreview(textures, {1}, [&](uint32_t width,uint32_t height) { renderWidth=width; renderHeight=height; return true; }));
    viewport.SetCameraPreviewTexture({2});
    auto logs = std::make_shared<BufferedLogBuffer>();
    ViewportPanel panel(viewport, tools, logs);
    LocalizationService text; assert(text.Load("Project/Resources/Editor/Localization")); text.SetLanguage("ja");
    PropertyDrawerRegistry drawers;
    EditorCommandRegistry commands;
    const EditorContext context{f.session, f.session.Selection(), f.properties, f.types, drawers, text, commands, f.edits};
    RenderScene scene; tools.Prepare(scene, 16.0f/9);
    ImGui::CreateContext();
    auto& io = ImGui::GetIO(); io.DisplaySize = {1200,850}; io.DeltaTime = 1.0f/60; io.IniFilename = nullptr;
    unsigned char* pixels; int w,h; io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h); io.Fonts->SetTexID(1);
    const auto frame = [&](ImVec2 mouse, bool down, float windowWidth = 1050) {
        io.AddMousePosEvent(mouse.x,mouse.y); io.AddMouseButtonEvent(0,down);
        ImGui::NewFrame(); ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({windowWidth,750});
        ImGui::Begin("PreviewTest"); panel.Draw(context); ImGui::End(); ImGui::Render();
    };
    frame({-100,-100}, false); frame({-100,-100}, false);
    frame({-100,-100}, false);
    ImGuiWindow* shelf = nullptr;
    ImGuiWindow* headerWindow = nullptr;
    for(auto* window : ImGui::GetCurrentContext()->Windows) {
        if(std::strstr(window->Name,"/ViewportToolShelf_")) shelf = window;
        if(std::strstr(window->Name,"/ViewportHeader_")) headerWindow = window;
    }
    assert(shelf && shelf->Active && headerWindow && headerWindow->Active);
    assert(shelf->Size.x < 50 && headerWindow->Size.x < 180);
    auto* sceneWindow = ImGui::FindWindowByName("PreviewTest");
    const auto sceneExtent = sceneWindow->WorkRect.GetSize();
    const auto renderExtent = std::pair{renderWidth,renderHeight};
    assert(shelf->Pos.x >= sceneWindow->WorkRect.Min.x && headerWindow->Pos.x+headerWindow->Size.x <= sceneWindow->WorkRect.Max.x);
    const auto chooseTool = [&](const char* id, int mode) {
        ImGui::FocusWindow(shelf);
        ImGui::ActivateItemByID(ImHashStr("##Tool",0,ImHashStr(id,0,shelf->ID)));
        frame({-100,-100},false); frame({-100,-100},false);
        assert(std::get<int64_t>(tools.CaptureForReinstancing().At("mode").data) == mode);
        assert(tools.ToolRegistry().Commands().IsChecked(id));
        assert(f.session.GetSelection() == other && !tools.IsManipulating());
    };
    chooseTool("viewport.select",-1); chooseTool("viewport.translate",0);
    chooseTool("viewport.rotate",1); chooseTool("viewport.scale",2);
    // アイコンの通常・有効・無効画像は、Controllerの実際の選択状態へ追従する。
    UI::SetEditorIcon("viewport.translate",ImTextureRef(static_cast<ImTextureID>(71)));
    UI::SetEditorIcon("viewport.translate.active",ImTextureRef(static_cast<ImTextureID>(72)));
    const auto shelfHasTexture = [&](ImTextureID id) {
        for(const auto& command : shelf->DrawList->CmdBuffer) if(command.GetTexID() == id && command.ElemCount) return true;
        return false;
    };
    chooseTool("viewport.translate",0); assert(shelfHasTexture(72) && !shelfHasTexture(71));
    chooseTool("viewport.rotate",1); assert(shelfHasTexture(71) && !shelfHasTexture(72));
    UI::SetEditorIcon("viewport.translate.inactive",ImTextureRef(static_cast<ImTextureID>(73)));
    frame({-100,-100},false); assert(shelfHasTexture(73) && !shelfHasTexture(71));
    UI::ClearEditorIcons();
    // Shelfへのマウス操作は背後の選択を変更しない。表示を展開してもSceneの画像領域は不変。
    const ImVec2 toggle{shelf->WorkRect.Min.x+8,shelf->WorkRect.Min.y+5};
    frame(toggle,false); frame(toggle,true); frame(toggle,false); frame({-100,-100},false);
    assert(shelf->Size.x > 80 && f.session.GetSelection() == other);
    assert(sceneWindow->WorkRect.GetSize().x == sceneExtent.x && sceneWindow->WorkRect.GetSize().y == sceneExtent.y);
    assert((std::pair{renderWidth,renderHeight} == renderExtent));
    ImGui::FocusWindow(shelf); ImGui::ActivateItemByID(shelf->GetID("###ExpandShelf"));
    frame({-100,-100},false); frame({-100,-100},false);
    assert(shelf->Size.x < 50);
    // DescriptorとCommandの登録だけで新ToolがShelfへ現れる。
    int extensionRuns = 0;
    assert(tools.RegisterTool({"viewport.measure","viewport.measure","viewport.measure","debug",ViewportToolGlyph::Other,0,"",{}},[&] { ++extensionRuns; }));
    assert(!tools.RegisterTool({"viewport.measure","viewport.measure","viewport.measure","debug",ViewportToolGlyph::Other,0,"",{}}));
    assert(!tools.RegisterTool({"viewport.conflict","viewport.measure","viewport.measure","debug",ViewportToolGlyph::Other,ImGuiKey_W,"W",{}}));
    frame({-100,-100},false); frame({-100,-100},false);
    chooseTool("viewport.measure",-1); assert(extensionRuns == 1);
    assert(!tools.ToolRegistry().Commands().IsChecked("viewport.scale"));
    chooseTool("viewport.translate",0);
    // 既存のW/E/RをShelfでToolを選択した後も使える。Popup入力中にはDispatchしない。
    for(const auto& [key,mode] : {std::pair{ImGuiKey_E,1},{ImGuiKey_R,2},{ImGuiKey_W,0}}) {
        io.AddKeyEvent(key,true); frame({-100,-100},false); frame({-100,-100},false);
        assert(std::get<int64_t>(tools.CaptureForReinstancing().At("mode").data) == mode);
        io.AddKeyEvent(key,false); frame({-100,-100},false);
    }
    const auto headerAction = [&](int scope,const char* item) {
        ImGui::FocusWindow(headerWindow);
        ImGui::ActivateItemByID(ImHashStr(item,0,ImHashData(&scope,sizeof(scope),headerWindow->ID)));
        frame({-100,-100},false); frame({-100,-100},false);
    };
    const float expandedHeaderWidth = headerWindow->Size.x;
    ImGui::FocusWindow(headerWindow); ImGui::ActivateItemByID(headerWindow->GetID("###CollapseOverlay"));
    frame({-100,-100},false); frame({-100,-100},false); frame({-100,-100},false);
    assert(headerWindow->Size.x < expandedHeaderWidth*.5f);
    assert((std::pair{renderWidth,renderHeight} == renderExtent));
    ImGui::FocusWindow(headerWindow); ImGui::ActivateItemByID(headerWindow->GetID("###CollapseOverlay"));
    frame({-100,-100},false); frame({-100,-100},false); frame({-100,-100},false);
    assert(std::abs(headerWindow->Size.x-expandedHeaderWidth) < 1);
    headerAction(3,"##Space");
    assert(!std::get<bool>(tools.CaptureForReinstancing().At("localSpace").data));
    headerAction(3,"##Space");
    assert(std::get<bool>(tools.CaptureForReinstancing().At("localSpace").data));
    headerAction(4,"##Snap");
    assert(std::get<bool>(tools.CaptureForReinstancing().At("snapping").data));
    assert(ImGui::GetCurrentContext()->OpenPopupStack.empty());
    headerAction(4,"##Snap");
    assert(!std::get<bool>(tools.CaptureForReinstancing().At("snapping").data));
    headerAction(5,"##View");
    assert(ImGui::GetCurrentContext()->OpenPopupStack.empty());
    headerAction(6,"##Display");
    assert(!std::get<bool>(tools.CaptureForReinstancing().At("gizmo").data));
    assert(ImGui::GetCurrentContext()->OpenPopupStack.empty());
    headerAction(6,"##Display");
    const int snapScope = 4;
    ImGui::FocusWindow(headerWindow);
    ImGui::ActivateItemByID(ImHashStr("##SnapDetails",0,ImHashData(&snapScope,sizeof(snapScope),headerWindow->ID)));
    frame({-100,-100},false); frame({-100,-100},false);
    assert(!ImGui::GetCurrentContext()->OpenPopupStack.empty());
    assert(f.session.GetSelection() == other && !tools.IsManipulating());
    auto* settingsPopup = ImGui::GetCurrentContext()->OpenPopupStack.back().Window;
    const auto verifyDetailPosition = [&] {
        auto* popup = ImGui::GetCurrentContext()->OpenPopupStack.back().Window;
        const float gap = UI::CurrentEditorTheme().viewportTools.detailPopupGap*ImGui::GetFontSize()/18;
        assert(popup && popup->Pos.y >= headerWindow->Pos.y+headerWindow->Size.y+gap-1);
    };
    verifyDetailPosition();
    ImGui::ActivateItemByID(settingsPopup->GetID(text.Text("viewport.snapEnabled").c_str()));
    frame({-100,-100},false);
    assert(std::get<bool>(tools.CaptureForReinstancing().At("snapping").data));
    io.AddKeyEvent(ImGuiKey_E,true); frame({-100,-100},false);
    assert(std::get<int64_t>(tools.CaptureForReinstancing().At("mode").data) == 0);
    io.AddKeyEvent(ImGuiKey_E,false); frame({-100,-100},false);
    ImGui::ClosePopupToLevel(0,true); frame({-100,-100},false);
    headerAction(5,"##ViewDetails");
    assert(!ImGui::GetCurrentContext()->OpenPopupStack.empty());
    verifyDetailPosition();
    // Popup表示中も同じ矢印をマウスでクリックして閉じられる。
    const auto chromeStyle = UI::CurrentEditorTheme().viewportTools;
    const float chromeScale = ImGui::GetFontSize()/18;
    const float pairWidth = (chromeStyle.button+chromeStyle.detailArrowWidth)*chromeScale;
    const ImVec2 viewArrow{headerWindow->WorkRect.Min.x+(chromeStyle.detailArrowWidth+chromeStyle.spacing)*chromeScale+
        2*(pairWidth+chromeStyle.spacing*chromeScale)+(chromeStyle.button+chromeStyle.detailArrowWidth*.5f)*chromeScale,
        headerWindow->WorkRect.Min.y+chromeStyle.button*chromeScale*.5f};
    frame(viewArrow,false); frame(viewArrow,true); frame(viewArrow,false);
    assert(ImGui::GetCurrentContext()->OpenPopupStack.empty());
    headerAction(6,"##DisplayDetails");
    assert(!ImGui::GetCurrentContext()->OpenPopupStack.empty());
    verifyDetailPosition();
    settingsPopup = ImGui::GetCurrentContext()->OpenPopupStack.back().Window;
    ImGui::ActivateItemByID(settingsPopup->GetID(text.Text("viewport.guides").c_str()));
    frame({-100,-100},false);
    assert(!std::get<bool>(tools.CaptureForReinstancing().At("guides").data));
    ImGui::ClosePopupToLevel(0,true); frame({-100,-100},false);
    ImGuiWindow* preview = nullptr;
    for(auto* window : ImGui::GetCurrentContext()->Windows)
        if(std::strstr(window->Name,"/CameraPreview_")) preview = window;
    assert(preview && preview->Active && viewport.IsCameraPreviewVisible());
    bool previewImage = false;
    for(const auto& command : preview->DrawList->CmdBuffer) if(command.GetTexID() == 12 && command.ElemCount) previewImage = true;
    assert(previewImage);
    // Shelf/Headerの外側では画像が入力を受け、空白クリックで従来どおり選択を解除できる。
    const ImVec2 scenePoint{sceneWindow->WorkRect.Min.x+sceneExtent.x*.45f,sceneWindow->WorkRect.Min.y+sceneExtent.y*.6f};
    frame(scenePoint,false); frame(scenePoint,true); frame(scenePoint,false);
    assert(!f.session.GetSelection().IsValid());
    f.session.Select(other);
    const ImVec2 point{preview->Pos.x+preview->Size.x*.5f, preview->Pos.y+preview->Size.y*.7f};
    frame(point,false); frame(point,true); frame(point,false);
    assert(f.session.GetSelection() == other && f.session.GetCameraPreview() == cameraId && !tools.IsManipulating());
    f.session.Diagnostics().Set("test", "active error", true);
    frame({-100,-100},false);
    bool diagnosticWindow = false;
    for(auto* window : ImGui::GetCurrentContext()->Windows)
        if(std::strstr(window->Name,"/ViewportDiagnostics_")) diagnosticWindow |= window->Active;
    assert(diagnosticWindow);
    // 履歴ログの重要度による色分けと、期限切れログの非表示を確認する。
    f.session.Diagnostics().Resolve("test");
    const auto now = std::chrono::system_clock::now();
    logs->Append({LogLevel::Warning, "test", "warning", now, std::source_location::current()});
    logs->Append({LogLevel::Error, "test", "error", now, std::source_location::current()});
    frame({-100,-100},false);
    ImGuiWindow* diagnostics = nullptr;
    for(auto* window : ImGui::GetCurrentContext()->Windows)
        if(std::strstr(window->Name,"/ViewportDiagnostics_")) diagnostics = window;
    assert(diagnostics && diagnostics->Active);
    bool red = false, yellow = false;
    for(const auto& vertex : diagnostics->DrawList->VtxBuffer) {
        red |= vertex.col == ImGui::ColorConvertFloat4ToU32({1,.32f,.3f,1});
        yellow |= vertex.col == ImGui::ColorConvertFloat4ToU32({1,.78f,.25f,1});
    }
    assert(red && yellow);
    logs->Clear();
    logs->Append({LogLevel::Error, "test", "expired", now-std::chrono::seconds(9), std::source_location::current()});
    frame({-100,-100},false);
    assert(!diagnostics->Active);
    const float closeWidth = std::max(ImGui::GetFrameHeight(), ImGui::CalcTextSize("X").x+ImGui::GetStyle().FramePadding.x*2);
    const ImVec2 close{preview->WorkRect.Max.x-closeWidth*.5f, preview->WorkRect.Min.y+ImGui::GetFrameHeight()*.5f};
    frame(close,false); frame(close,true); frame(close,false);
    frame({-100,-100},false);
    assert(!f.session.GetCameraPreview().IsValid());
    assert(!viewport.IsCameraPreviewVisible());
    frame({-100,-100}, false, 260); frame({-100,-100}, false, 260); frame({-100,-100}, false, 260);
    assert(shelf->Active && headerWindow->Active);
    assert(headerWindow->Pos.x >= shelf->Pos.x+shelf->Size.x && headerWindow->Pos.x+headerWindow->Size.x <= 260);
    ImGui::DestroyContext();
    std::cout << "PASS: pinned camera preview lifecycle, input isolation, close control, severity colours and log expiry\n";
}
}
