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
    assert(viewport.AttachLocalPreview(textures, {1}, [](uint32_t,uint32_t) { return true; }));
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
    const auto frame = [&](ImVec2 mouse, bool down) {
        io.AddMousePosEvent(mouse.x,mouse.y); io.AddMouseButtonEvent(0,down);
        ImGui::NewFrame(); ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({1050,750});
        ImGui::Begin("PreviewTest"); panel.Draw(context); ImGui::End(); ImGui::Render();
    };
    frame({-100,-100}, false); frame({-100,-100}, false);
    ImGuiWindow* preview = nullptr;
    for(auto* window : ImGui::GetCurrentContext()->Windows)
        if(std::strstr(window->Name,"/CameraPreview_")) preview = window;
    assert(preview && preview->Active && viewport.IsCameraPreviewVisible());
    bool previewImage = false;
    for(const auto& command : preview->DrawList->CmdBuffer) if(command.GetTexID() == 12 && command.ElemCount) previewImage = true;
    assert(previewImage);
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
    ImGui::DestroyContext();
    std::cout << "PASS: pinned camera preview lifecycle, input isolation, close control, severity colours and log expiry\n";
}
}
