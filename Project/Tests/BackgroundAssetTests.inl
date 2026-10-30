#include <future>
#include "Graphics/Model/ModelAssetLibrary.h"

namespace {
void BackgroundAssetTests(bool warp) {
    struct Window {
        // ウィンドウを表示せず、実際のSwapChainとGPU検証を使用する。
        HWND handle = CreateWindowExW(0, L"STATIC", L"Asset preparation test", WS_POPUP,
            0, 0, 160, 120, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        ~Window() { if(handle) DestroyWindow(handle); }
    } window;
    Require(window.handle != nullptr, "Cannot create asset preparation test window.");
    auto oldAsset = PrimitiveMeshBuilder::Build(PrimitiveShape::Cube); Check(oldAsset);
    ModelAssetLibrary library("Generated", *oldAsset);
    Check(library.Resolve(PrimitiveShape::Sphere, "", ""));
    Result<ModelAssetData> newAsset = library.Snapshot();
    // 画像デコードと全mip生成が、描画の継続中に実行されるケースを作る。
    constexpr uint32_t side = 512, rowBytes = side * 3, imageBytes = side * rowBytes, fileBytes = 54 + imageBytes;
    auto& bitmap = newAsset->materials.back().baseColorImageData;
    bitmap.resize(fileBytes);
    bitmap[0] = 'B'; bitmap[1] = 'M'; bitmap[10] = 54; bitmap[14] = 40;
    bitmap[26] = 1; bitmap[28] = 24;
    std::memcpy(bitmap.data() + 2, &fileBytes, 4);
    std::memcpy(bitmap.data() + 18, &side, 4); std::memcpy(bitmap.data() + 22, &side, 4);
    std::memcpy(bitmap.data() + 34, &imageBytes, 4);
    for(uint32_t y = 0; y < side; ++y) for(uint32_t x = 0; x < side; ++x) {
        const auto offset = 54 + size_t(y) * rowBytes + size_t(x) * 3;
        bitmap[offset] = static_cast<uint8_t>(x); bitmap[offset + 1] = static_cast<uint8_t>(y); bitmap[offset + 2] = 128;
    }
    ModelInstance oldPose, newPose; Check(oldPose.Initialize(*oldAsset)); Check(newPose.Initialize(*newAsset));
    RenderScene scene; scene.camera = RenderCamera{};
    scene.primitives.push_back({{1}, Matrix4x4::Identity(), {1,1,1,1}});
    struct InitialRenderer final : IGraphicsRenderer {
        PrimitiveRenderer renderer;
        ComPtr<ID3D12InfoQueue> info;
        bool renderGeometry = true;
        Result<void> Initialize(const GraphicsRendererInitializationContext& context) override {
            Native(GraphicsBackendTestAccess::Device(context.resources)->QueryInterface(IID_PPV_ARGS(&info)));
            Native(info->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, FALSE));
            Native(info->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, FALSE));
            return renderer.Initialize(context);
        }
        void Render(GraphicsContext& context) override {
            if(renderGeometry) renderer.Render(context);
            else context.RestoreMainRenderTargets();
        }
        void Shutdown() noexcept override { renderer.Shutdown(); }
    } initial;
    // ソフトウェア側は転送の競合検査に限定し、大規模なPSの実行時コンパイルを避ける。
    initial.renderGeometry = !warp;
    auto& previous = initial.renderer;
    PrimitiveRenderer prepared;
    previous.SetModelInput(&*oldAsset, &oldPose); previous.SetRenderScene(&scene);
    prepared.SetModelInput(&*newAsset, &newPose); prepared.SetRenderScene(&scene);
    GraphicsSystem graphics;
    GraphicsSystemDesc description;
    description.useWarpAdapter = warp; description.enableVSync = false;
    description.enableDebugLayer = true; description.enableGpuBasedValidation = true;
    description.shaderDirectory = L"Project/Shaders";
    Check(graphics.Initialize({window.handle, 160, 120}, description));
    Check(graphics.AttachRenderer(&initial));
    auto settings = previous.GetShadows().GetSettings(); settings.shadowMapResolution = 256;
    if(warp) {
        // 転送と描画の並行性が対象なので、ソフトウェア側では高負荷のDXR実行を省く。
        // 実機側ではDXRを有効にし、ASキャッシュの引き継ぎも検証する。
        settings.mode = ShadowMode::ShadowMap;
        auto optics = previous.GetOptics().GetSettings(); optics.enabled = false;
        Check(previous.GetOptics().SetSettings(optics));
    }
    Check(previous.GetShadows().SetSettings(settings));
    if(!warp && previous.AreMeshShadersAvailable()) {
        // 初回有効化を遅延し、その後のモデル追加で既存Meshletも再利用する。
        previous.GetMeshletSettings().enabled = true;
        previous.GetMeshletSettings().enableCulling = false;
    }
    for(int i = 0; i < 3; ++i) Check(graphics.RenderFrame());
    Check(graphics.WaitForIdle());
    const auto originalMeshlets = GraphicsBackendTestAccess::Meshlets(previous,0);
    std::promise<void> gate;
    auto allowed = gate.get_future();
    const auto start = std::chrono::steady_clock::now();
    auto preparation = std::async(std::launch::async, [&graphics, &prepared, reuse = previous.CaptureModelResources(), allowed = std::move(allowed)]() mutable {
        allowed.wait();
        return graphics.PrepareModelResources(&prepared, std::move(reuse));
    });
    struct GateGuard {
        std::promise<void>& promise;
        bool released = false;
        void Release() { promise.set_value(); released = true; }
        ~GateGuard() { if(!released) { try { promise.set_value(); } catch(...) {} } }
    } guard{gate};
    // 読み込みが停止していても、描画側が完了を待たないことを検証する。
    for(int i = 0; i < 3; ++i) Check(graphics.RenderFrame());
    Require(preparation.wait_for(std::chrono::seconds(0)) != std::future_status::ready,
        "Asset preparation gate did not isolate the worker.");
    guard.Release();
    size_t frames = 0;
    double longest = 0;
    while(preparation.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        const auto frameStart = std::chrono::steady_clock::now();
        Check(graphics.RenderFrame()); ++frames;
        longest = (std::max)(longest, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frameStart).count());
        Require(std::chrono::steady_clock::now() - start < std::chrono::seconds(60), "Background preparation timed out.");
    }
    Check(preparation.get());
    Require(frames > 0, "No frames rendered during texture and mesh preparation.");
    const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    const auto publicationStart = std::chrono::steady_clock::now();
    Check(graphics.WaitForIdle());
    const auto profile = prepared.GetInitializationProfile();
    Require(profile.meshesUploaded == 1 && profile.meshesReused == 1 && profile.texturesCreated == 1 && profile.texturesReused == 1,
        "Background preparation did not reuse the existing mesh and texture.");
    Require(profile.pipelineMs == 0 && profile.meshletPipelineMs == 0, "Background model preparation recreated a pipeline.");
    previous.CommitPreparedModel(prepared);
    prepared.Shutdown();
    if(originalMeshlets) Require(GraphicsBackendTestAccess::Meshlets(previous,0)==originalMeshlets,"Unchanged meshlets were regenerated.");
    Require(previous.GetShadows().GetSettings().shadowMapResolution == 256, "Mesh replacement reset view settings.");
    if(!warp) Require(previous.GetShadows().GetStatistics().blasCount == 1, "Existing BLAS was rebuilt or dropped during publication.");
    const double publication = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - publicationStart).count();
    // 実機では新メッシュを描画し、ASキャッシュとDescriptorの寿命もGPU検証で確認する。
    if(warp) scene.camera.reset();
    for(int i = 0; i < 5; ++i) Check(graphics.RenderFrame());
    Check(graphics.WaitForIdle());
    auto info = initial.info;
    graphics.Shutdown();
    for(uint64_t i = 0; i < info->GetNumStoredMessages(); ++i) {
        SIZE_T bytes = 0; Native(info->GetMessage(i, nullptr, &bytes));
        std::vector<uint8_t> storage(bytes); auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
        Native(info->GetMessage(i, message, &bytes));
        if(message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) throw std::runtime_error(message->pDescription);
    }
    std::cout << "PASS: background asset preparation (" << (warp ? "WARP" : "hardware") << "): " << elapsed
        << " ms, rendered " << frames << " frames, longest frame=" << longest << " ms, publication=" << publication << " ms\n";
}
}
