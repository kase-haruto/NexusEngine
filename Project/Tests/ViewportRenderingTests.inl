namespace {
void ViewportRenderingTests(bool warp) {
    struct Window {
        // 描画基盤の検証用。画面には表示しないウィンドウを作成する。
        HWND handle = CreateWindowExW(0, L"STATIC", L"Viewport test", WS_POPUP, 0, 0, 320, 240, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        ~Window() { if(handle) DestroyWindow(handle); }
    } window;
    Require(window.handle != nullptr, "Test window creation failed.");
    struct Renderer final : IGraphicsRenderer {
        ComPtr<ID3D12InfoQueue> info;
        ConstantBuffer buffer;
        uint32_t width = 0, height = 0;
        int renders = 0, submissions = 0, abandoned = 0, shutdowns = 0;
        bool fail = false;
        Result<void> Initialize(const GraphicsRendererInitializationContext& context) override {
            if(fail) return std::unexpected(Error(ErrorCategory::Graphics, 990, "Expected preview initialization failure."));
            Native(GraphicsBackendTestAccess::Device(context.resources)->QueryInterface(IID_PPV_ARGS(&info)));
            Native(info->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, FALSE));
            Native(info->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, FALSE));
            // 既存のHDR描画先は最適化用クリア値を持たない。この性能通知だけを除外する。
            D3D12_MESSAGE_ID advisory = D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE;
            D3D12_INFO_QUEUE_FILTER filter{};
            filter.DenyList.NumIDs = 1; filter.DenyList.pIDList = &advisory;
            Native(info->AddStorageFilterEntries(&filter));
            return context.resources.CreateConstantBuffer(buffer, 256);
        }
        void Render(GraphicsContext& context) override {
            width = context.GetRenderWidth(); height = context.GetRenderHeight(); ++renders;
            context.RestoreMainRenderTargets();
            // 合計400件を確保し、各ビューの256件の使用枠が独立していることを検証する。
            GraphicsBackendTestAccess::AllocateViewDescriptors(context, buffer);
            GraphicsBackendTestAccess::Listen(context, this, [](void* owner, bool submitted, uint32_t) {
                auto& renderer = *static_cast<Renderer*>(owner);
                if(submitted) ++renderer.submissions; else ++renderer.abandoned;
            });
        }
        void Shutdown() noexcept override { buffer.Shutdown(); ++shutdowns; }
    } main, preview, replacement, failed;
    GraphicsSystem graphics;
    GraphicsSystemDesc description;
    description.useWarpAdapter = warp; description.enableVSync = false;
    description.enableDebugLayer = true; description.enableGpuBasedValidation = true;
    description.shaderDirectory = L"Project/Shaders";
    Check(graphics.Initialize({window.handle,320,240}, description));
    Check(graphics.EnableSceneViewport()); Check(graphics.AttachRenderer(&main));
    Check(graphics.ReplaceCameraPreviewRenderer(&preview));
    graphics.SetCameraPreviewActive(true);
    const auto image = graphics.GetCameraPreviewImage();
    Require(image.width == 640 && image.height == 360 && image.backendReference, "Preview texture is unavailable.");
    Require(image.backendReference != graphics.GetSceneViewportImage().backendReference, "Preview shares the main colour target.");
    for(int i=0;i<3;++i) Check(graphics.RenderFrame());
    Check(graphics.WaitForIdle());
    Require(main.renders == 3 && preview.renders == 3 && preview.submissions == 3 && !preview.abandoned,
        "Preview render submission lifecycle is incorrect.");
    Require(main.width == 320 && main.height == 240 && preview.width == 640 && preview.height == 360,
        "Main and preview dimensions interfere.");
    Require(graphics.RequestSceneViewportSize(400,250), "Main resize request failed.");
    Check(graphics.PrepareSceneViewport()); Check(graphics.RenderFrame());
    Require(main.width == 400 && preview.width == 640, "Resizing the main viewport changed preview framing.");
    graphics.SetCameraPreviewActive(false); Check(graphics.RenderFrame());
    Require(preview.renders == 4 && !graphics.GetCameraPreviewImage().backendReference, "Disabled preview still renders.");
    failed.fail = true;
    Require(!graphics.ReplaceCameraPreviewRenderer(&failed), "Failed preview replacement was accepted.");
    graphics.SetCameraPreviewActive(true); Check(graphics.RenderFrame());
    Require(preview.renders == 5 && preview.shutdowns == 0, "Failed replacement destroyed the active preview.");
    Check(graphics.ReplaceCameraPreviewRenderer(&replacement)); Check(graphics.RenderFrame()); Check(graphics.WaitForIdle());
    Require(preview.shutdowns == 1 && replacement.renders == 1 && replacement.submissions == 1,
        "Preview replacement did not retire the old renderer safely.");
    // GPU検証を有効にして、深度・HDR・出力先の状態遷移と解放順序も確認する。
    auto info = main.info;
    graphics.Shutdown();
    Require(main.shutdowns == 1 && replacement.shutdowns == 1, "Preview shutdown lifecycle is incorrect.");
    bool validationFailed = false;
    for(uint64_t i=0;i<info->GetNumStoredMessages();++i) {
        SIZE_T bytes = 0; Native(info->GetMessage(i,nullptr,&bytes));
        std::vector<uint8_t> storage(bytes); auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
        Native(info->GetMessage(i,message,&bytes));
        if(message->Severity <= D3D12_MESSAGE_SEVERITY_WARNING) { std::cerr << message->pDescription << '\n'; validationFailed = true; }
    }
    Require(!validationFailed, "Viewport rendering generated a GPU validation warning/error.");
    std::cout << "PASS: independent viewport targets, resize, preview visibility, failed/successful replacement and submission lifecycle ("
        << (warp ? "WARP" : "hardware") << ")\n";
}
}
