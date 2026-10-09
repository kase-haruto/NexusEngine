#include "Graphics/Color/ColorSpace.h"
#include "Graphics/Backend/DirectX12/PostProcess/PostProcessBackend.h"
#include "Graphics/Resource/TextureResource.h"
#include "Graphics/Resource/ConstantBuffer.h"
#include <limits>

namespace {
void ColorManagementIntegration() {
    Require(std::abs(ColorSpace::SrgbToLinear(0.5f) - 0.21404114f) < 1e-6f, "sRGB midpoint decode is incorrect.");
    for(int i = 0; i <= 255; ++i) {
        float value = static_cast<float>(i) / 255;
        Require(std::abs(ColorSpace::LinearToSrgb(ColorSpace::SrgbToLinear(value)) - value) < 1e-6f, "sRGB round trip failed.");
    }
    auto alpha = ColorSpace::SrgbToLinear(std::array<float, 4>{0.5f, 0.5f, 0.5f, 0.3f});
    Require(alpha[3] == 0.3f, "Colour conversion modified alpha.");
    PostProcessSettings invalid;
    invalid.toneMapper = static_cast<ToneMapper>(99);
    Require(!invalid.IsValid(), "Unknown tone mapper accepted.");
    invalid = {}; invalid.exposureEV = std::numeric_limits<float>::infinity();
    Require(!invalid.IsValid(), "Non-finite exposure accepted.");

    Fixture test(false);
    // Reference patches deliberately vary the clear colour. Ignore only this
    // fast-clear performance advisory; resource/binding/colour errors still fail.
    if(test.info) {
        D3D12_MESSAGE_ID advisory = D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE;
        D3D12_INFO_QUEUE_FILTER filter {};
        filter.DenyList.NumIDs = 1; filter.DenyList.pIDList = &advisory;
        Native(test.info->AddStorageFilterEntries(&filter));
    }
    PostProcessBackend post;
    Check(post.Initialize(test.device.GetDevice(), L"Project/Shaders", 8, 8));
    ShadowTexture depth; Check(test.shadows.CreateTexture(depth, 8, 8, true));
    auto dsv = test.descriptors.DepthStencils().GetCpuHandle(depth.dsv);
    auto outputSlot = test.descriptors.RenderTargets().Allocate(); Check(outputSlot);
    auto outputRtv = test.descriptors.RenderTargets().GetCpuHandle(*outputSlot);
    D3D12_HEAP_PROPERTIES heap {}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; desc.Width = 8; desc.Height = 8;
    desc.DepthOrArraySize = 1; desc.MipLevels = 1; desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1; desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    ComPtr<ID3D12Resource> output;
    Native(test.device.GetDevice()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&output)));
    test.device.GetDevice()->CreateRenderTargetView(output.Get(), nullptr, outputRtv);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint {}; uint64_t bytes = 0;
    test.device.GetDevice()->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC readDesc {}; readDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readDesc.Width = bytes; readDesc.Height = 1; readDesc.DepthOrArraySize = 1; readDesc.MipLevels = 1;
    readDesc.SampleDesc.Count = 1; readDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> readback;
    Native(test.device.GetDevice()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &readDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)));

    // Same encoded pixels, different semantics: colours decode sRGB, data stay linear.
    std::vector<uint8_t> bmp(58, 0);
    bmp[0] = 'B'; bmp[1] = 'M'; bmp[2] = 58; bmp[10] = 54; bmp[14] = 40;
    bmp[18] = 1; bmp[22] = 1; bmp[26] = 1; bmp[28] = 24;
    bmp[54] = bmp[55] = bmp[56] = 128;
    TextureResource colour, data;
    Check(test.factory.CreateTexture2DFromEncodedData(colour, bmp, TextureColorSpace::Srgb));
    Check(test.factory.CreateTexture2DFromEncodedData(data, bmp, TextureColorSpace::Linear));
    auto colourRef = test.factory.CreatePersistentTextureShaderResource(colour); Check(colourRef);
    auto dataRef = test.factory.CreatePersistentTextureShaderResource(data); Check(dataRef);
    Shader vs, ps;
    Check(vs.Initialize({L"Project/Tests/ColorSpaceTest.hlsl", L"VSMain", L"vs_6_6"}, ShaderStage::Vertex));
    Check(ps.Initialize({L"Project/Tests/ColorSpaceTest.hlsl", L"PSMain", L"ps_6_6"}, ShaderStage::Pixel));
    GraphicsPipeline samplePipeline;
    GraphicsPipelineDesc pipelineDesc; pipelineDesc.hdrRenderTarget = true;
    pipelineDesc.depthStencilFormat = DepthStencilFormat::D32Float;
    Check(test.factory.CreateGraphicsPipeline(samplePipeline, std::move(vs), std::move(ps), pipelineDesc));
    ConstantBuffer selection; Check(test.factory.CreateConstantBuffer(selection, 16));
    TransientDescriptorArena transient; Check(transient.Initialize(test.device.GetDevice(), &test.descriptors.Resources(), 2, 16));

    auto render = [&](std::array<float, 4> linear, PostProcessSettings settings, uint32_t texture = 0) {
        Check(test.queue.WaitForIdle()); Native(test.allocator->Reset()); Native(test.commands->Reset(test.allocator.Get(), nullptr));
        post.BeginScene(test.commands.Get(), dsv, linear.data());
        if(texture != 0) {
            transient.ResetFrame(0); test.bindless.Bind(test.commands.Get());
            auto context = GraphicsBackendTestAccess::Context(test.commands.Get(), test.bindless, 0, 8, 8);
            GraphicsBackendTestAccess::Targets(context, post.GetSceneRtv().ptr, dsv.ptr, &transient);
            context.RestoreMainRenderTargets(); context.SetGraphicsPipeline(samplePipeline);
            context.SetPrimitiveTopology(PrimitiveTopology::TriangleList);
            std::array<uint32_t, 4> indices {texture, 0, 0, 0};
            Check(selection.Write(0, {reinterpret_cast<const uint8_t*>(indices.data()), sizeof(indices)}));
            Check(context.SetGraphicsConstantBufferTable(samplePipeline, selection)); context.Draw(3);
        }
        post.Resolve(test.commands.Get(), outputRtv, settings);
        D3D12_RESOURCE_BARRIER barrier {}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition = {output.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE};
        test.commands->ResourceBarrier(1, &barrier);
        D3D12_TEXTURE_COPY_LOCATION src {}, dst {};
        src.pResource = output.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.pResource = readback.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint = footprint;
        test.commands->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter); test.commands->ResourceBarrier(1, &barrier);
        Native(test.commands->Close()); test.queue.Execute(test.commands.Get()); Check(test.queue.WaitForIdle());
        void* mapped = nullptr; Native(readback->Map(0, nullptr, &mapped));
        std::array<uint8_t, 4> pixel {};
        std::memcpy(pixel.data(), static_cast<uint8_t*>(mapped) + footprint.Offset + footprint.Footprint.RowPitch * 4 + 4 * 4, 4);
        readback->Unmap(0, nullptr); return pixel;
    };
    auto withinByteTolerance = [](uint8_t actual, int expected) { return std::abs(int(actual) - expected) <= 1; };
    PostProcessSettings settings; settings.bloomEnabled = false; settings.antiAliasingEnabled = false;
    settings.toneMapper = ToneMapper::LinearClamp;
    Require(withinByteTolerance(render({0.21404114f, 0.21404114f, 0.21404114f, 1}, settings)[0], 128), "SDR output has incorrect or duplicate sRGB encoding.");
    Require(withinByteTolerance(render({}, settings, colourRef->index)[0], 128), "Colour texture was not decoded as sRGB.");
    Require(withinByteTolerance(render({}, settings, dataRef->index)[0], 188), "Data texture incorrectly received sRGB decoding.");
    settings.exposureEV = 1;
    Require(withinByteTolerance(render({0.09f, 0.09f, 0.09f, 1}, settings)[0], 118), "Exposure EV did not scale linear light.");
    settings.exposureEV = 0; settings.toneMapper = ToneMapper::PbrNeutral;
    auto neutral = render({0.22f, 0.22f, 0.22f, 1}, settings);
    Require(withinByteTolerance(neutral[0], 118) && neutral[0] == neutral[1] && neutral[1] == neutral[2], "PBR Neutral reference grey is incorrect.");
    auto acesSettings = settings; acesSettings.toneMapper = ToneMapper::AcesFitted;
    auto aces = render({0.22f, 0.22f, 0.22f, 1}, acesSettings);
    Require(aces[0] < neutral[0] - 5, "Tone mapping selection was not applied.");
    auto chromatic = render({0.2f, 0.4f, 4, 1}, settings);
    float r = ColorSpace::SrgbToLinear(static_cast<float>(chromatic[0]) / 255);
    float g = ColorSpace::SrgbToLinear(static_cast<float>(chromatic[1]) / 255);
    float b = ColorSpace::SrgbToLinear(static_cast<float>(chromatic[2]) / 255);
    Require(std::abs((g - r) / (b - r) - 0.2f / 3.8f) < 0.015f, "Neutral tone mapping changed hue.");
    uint8_t previous = 0;
    for(float light : {0.0f, 0.001f, 0.01f, 0.18f, 0.76f, 1.0f, 4.0f, 100.0f, 65504.0f}) {
        auto pixel = render({light, light, light, 1}, settings);
        Require(pixel[0] >= previous && pixel[0] == pixel[1] && pixel[1] == pixel[2] && pixel[3] == 255, "Neutral HDR response is not monotonic/achromatic.");
        previous = pixel[0];
    }
    settings.antiAliasingEnabled = true;
    Require(render({0.22f, 0.22f, 0.22f, 1}, settings) == neutral, "FXAA modified a uniform colour patch.");
    settings.bloomEnabled = true;
    Require(render({4, 4, 4, 1}, settings)[0] > 240, "Bloom/tone mapping produced invalid HDR output.");
    settings.bloomEnabled = false; settings.filmicEnabled = false;
    Require(withinByteTolerance(render({0.18f, 0.18f, 0.18f, 1}, settings)[0], 118), "Legacy linear clamp override was ignored.");
    Check(post.Resize(test.device.GetDevice(), 8, 8));
    Require(withinByteTolerance(render({0.18f, 0.18f, 0.18f, 1}, settings)[0], 118), "Resize broke colour processing.");
    post.Shutdown(); samplePipeline.Shutdown(); transient.Shutdown();
    Check(test.factory.RetirePersistentShaderResource(*colourRef)); Check(test.factory.RetirePersistentShaderResource(*dataRef));
    test.shadows.ReleaseTexture(depth); Check(test.descriptors.RenderTargets().Free(*outputSlot));
    test.Validate();
    std::cout << "Colour space, GPU texture decoding, tone mapping, exposure, FXAA and resize passed.\n";
}
}
