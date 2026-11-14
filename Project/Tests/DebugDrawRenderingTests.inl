#include "Graphics/Debug/DebugDrawRenderer.h"
#include "Graphics/Backend/DirectX12/PostProcess/PostProcessBackend.h"

namespace {
/** \brief GPU readbackで実際の線・深度遮蔽・overlay・空frameの消去を確認する */
void DebugDrawRenderingTests() {
    Fixture test(false);
    if(test.info) {
        D3D12_MESSAGE_ID advisories[]{D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
            D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE};
        D3D12_INFO_QUEUE_FILTER filter{};
        filter.DenyList.NumIDs = 2;
        filter.DenyList.pIDList = advisories;
        Native(test.info->AddStorageFilterEntries(&filter));
    }
    constexpr uint32_t size = 256;
    DebugDrawRenderer renderer;
    Check(renderer.Initialize({test.factory, std::filesystem::path(L"Project/Shaders"), {}}));
    PostProcessBackend post;
    Check(post.Initialize(test.device.GetDevice(), L"Project/Shaders", size, size));
    ShadowTexture depth;
    Check(test.shadows.CreateTexture(depth, size, size, true));
    const auto dsv = test.descriptors.DepthStencils().GetCpuHandle(depth.dsv);
    auto slot = test.descriptors.RenderTargets().Allocate();
    Check(slot);
    const auto rtv = test.descriptors.RenderTargets().GetCpuHandle(*slot);
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = desc.Height = size;
    desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    ComPtr<ID3D12Resource> output;
    Native(test.device.GetDevice()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&output)));
    test.device.GetDevice()->CreateRenderTargetView(output.Get(), nullptr, rtv);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    uint64_t bytes = 0;
    test.device.GetDevice()->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = bytes;
    buffer.Height = buffer.SampleDesc.Count = 1;
    buffer.DepthOrArraySize = buffer.MipLevels = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> readback;
    Native(test.device.GetDevice()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)));
    TransientDescriptorArena arena;
    Check(arena.Initialize(test.device.GetDevice(), &test.descriptors.Resources(), 2, 8));
    const auto render = [&](std::span<const DebugDrawLine> lines) {
        Check(test.queue.WaitForIdle());
        Native(test.allocator->Reset());
        Native(test.commands->Reset(test.allocator.Get(), nullptr));
        const std::array<float, 4> clear{0, 0, 0, 1};
        post.BeginScene(test.commands.Get(), dsv, clear.data());
        test.commands->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, .2f, 0, 0, nullptr);
        D3D12_VIEWPORT viewport{0, 0, float(size), float(size), 0, 1};
        D3D12_RECT scissor{0, 0, LONG(size), LONG(size)};
        test.commands->RSSetViewports(1, &viewport);
        test.commands->RSSetScissorRects(1, &scissor);
        arena.ResetFrame(0);
        test.bindless.Bind(test.commands.Get());
        auto context = GraphicsBackendTestAccess::Context(test.commands.Get(), test.bindless, 0, size, size);
        GraphicsBackendTestAccess::Targets(context, post.GetSceneRtv().ptr, dsv.ptr, &arena);
        Check(renderer.Render(context, Matrix4x4::Identity(), lines));
        PostProcessSettings settings;
        settings.bloomEnabled = settings.antiAliasingEnabled = settings.filmicEnabled = false;
        post.Resolve(test.commands.Get(), rtv, settings);
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition = {output.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE};
        test.commands->ResourceBarrier(1, &barrier);
        D3D12_TEXTURE_COPY_LOCATION source{}, destination{};
        source.pResource = output.Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.pResource = readback.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destination.PlacedFootprint = footprint;
        test.commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        test.commands->ResourceBarrier(1, &barrier);
        Native(test.commands->Close());
        test.queue.Execute(test.commands.Get());
        Check(test.queue.WaitForIdle());
        void* mapped = nullptr;
        Native(readback->Map(0, nullptr, &mapped));
        std::array<size_t, 3> colors{};
        for(uint32_t row = 0; row < size; ++row) {
            const auto* pixels = static_cast<uint8_t*>(mapped) + footprint.Offset + row * footprint.Footprint.RowPitch;
            for(uint32_t column = 0; column < size; ++column) {
                const auto* pixel = pixels + column * 4;
                for(size_t channel = 0; channel < 3; ++channel) {
                    if(pixel[channel] > 40 && pixel[channel] > pixel[(channel + 1) % 3] * 2 && pixel[channel] > pixel[(channel + 2) % 3] * 2) {
                        ++colors[channel];
                    }
                }
            }
        }
        readback->Unmap(0, nullptr);
        return colors;
    };
    const std::array<DebugDrawLine, 3> lines{{
        {{-.8f, 0, .1f}, {.8f, 0, .1f}, {1, 0, 0, 1}, true},
        {{-.8f, .4f, .8f}, {.8f, .4f, .8f}, {0, 1, 0, 1}, true},
        {{-.8f, -.4f, .8f}, {.8f, -.4f, .8f}, {0, 0, 1, 1}, false}}};
    const auto colors = render(lines);
    test.Validate();
    const auto overlayOnly = render({lines.data() + 2, 1});
    Require(overlayOnly[0] == 0 && overlayOnly[1] == 0 && overlayOnly[2] > 150, "Overlay-only pass failed.");
    Require(colors[0] > 150 && colors[1] == 0 && colors[2] > 150, "Debug lines failed raster/depth/overlay readback.");
    const auto empty = render({});
    Require(empty[0] == 0 && empty[1] == 0 && empty[2] == 0, "Empty frame retained old debug lines.");
    renderer.Shutdown();
    post.Shutdown();
    arena.Shutdown();
    test.shadows.ReleaseTexture(depth);
    Check(test.descriptors.RenderTargets().Free(*slot));
    test.Validate();
    std::cout << "PASS: debug line GPU raster, depth occlusion, overlay and empty-frame cleanup.\n";
}
}
