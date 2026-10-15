#include "ViewportTarget.h"
#include <utility>

namespace NexusEngine {
Result<void> ViewportTarget::Resize(ID3D12Device* device, DescriptorAllocator& resources, DescriptorAllocator& renderTargets,
    uint32_t width, uint32_t height) {
    if(!device || !width || !height || width > 16384 || height > 16384 ||
        (resources_ && resources_ != &resources) || (renderTargets_ && renderTargets_ != &renderTargets))
        return std::unexpected(Error(ErrorCategory::Graphics, 100, "Invalid viewport target dimensions or allocator."));
    if(resource_ && width == width_ && height == height_) return {};
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT; heap.CreationNodeMask = heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width; desc.Height = height; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    Microsoft::WRL::ComPtr<ID3D12Resource> replacement;
    auto hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&replacement));
    if(FAILED(hr)) return std::unexpected(MakeDirectXError(101, hr, "Failed to create scene viewport render target."));
    auto srv = srv_, rtv = rtv_;
    if(!srv.IsValid()) {
        auto allocation = resources.Allocate();
        if(!allocation) return std::unexpected(std::move(allocation.error()));
        srv = *allocation;
        auto renderAllocation = renderTargets.Allocate();
        if(!renderAllocation) {
            static_cast<void>(resources.Free(srv));
            return std::unexpected(std::move(renderAllocation.error()));
        }
        rtv = *renderAllocation;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = desc.Format; view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; view.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(replacement.Get(), &view, srv.cpuHandle);
    device->CreateRenderTargetView(replacement.Get(), nullptr, rtv.cpuHandle);
    // 呼出側はGPU idleを保証済み。旧画像とViewへの未完了参照がない時だけcommitする。
    resource_ = std::move(replacement); srv_ = srv; rtv_ = rtv;
    resources_ = &resources; renderTargets_ = &renderTargets; width_ = width; height_ = height;
    return {};
}
namespace {
void Transition(ID3D12GraphicsCommandList* commands, ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) noexcept {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
    commands->ResourceBarrier(1, &barrier);
}
}
void ViewportTarget::BeginWrite(ID3D12GraphicsCommandList* commands) const noexcept {
    Transition(commands, resource_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
}
void ViewportTarget::EndWrite(ID3D12GraphicsCommandList* commands) const noexcept {
    Transition(commands, resource_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}
void ViewportTarget::Shutdown() noexcept {
    resource_.Reset();
    if(resources_ && srv_.IsValid()) static_cast<void>(resources_->Free(srv_));
    if(renderTargets_ && rtv_.IsValid()) static_cast<void>(renderTargets_->Free(rtv_));
    srv_ = {}; rtv_ = {}; resources_ = nullptr; renderTargets_ = nullptr; width_ = height_ = 0;
}
}
