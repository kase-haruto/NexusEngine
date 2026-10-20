#include "Graphics/Meshlet/MeshletResource.h"
#include "Graphics/Resource/ImmediateUploadContext.h"
#include <array>
#include <cmath>
#include <cstring>
#include <span>
#include <wrl/client.h>
namespace NexusEngine {
class MeshletResource::Impl {
public:
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 4> buffers; //< metadata / remap / primitives / bounds
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 4> staging; //< Upload 完了確認失敗時も保持
    std::unique_ptr<ImmediateUploadContext> upload; //< 投入済み List / Allocator の寿命保持
    std::vector<MeshletRange> ranges; //< Material ごとの Meshlet 範囲
    uint64_t generation = 0; //< 元 Geometry の世代
};
MeshletResource::MeshletResource() noexcept = default;
MeshletResource::~MeshletResource() noexcept = default;
void MeshletResource::Shutdown() noexcept { impl_.reset(); }
const std::vector<MeshletRange>& MeshletResource::GetRanges() const noexcept {
    static const std::vector<MeshletRange> empty;
    return impl_ ? impl_->ranges : empty;
}
uint64_t MeshletResource::GetGeometryGeneration() const noexcept { return impl_ ? impl_->generation : 0; }
uint64_t MeshletResource::GetGpuAddress(uint32_t buffer) const noexcept {
    return impl_ && buffer < 4 ? impl_->buffers[buffer]->GetGPUVirtualAddress() : 0;
}
Result<void> MeshletResource::Initialize(void* native, CommandQueue* queue, const MeshletData& data, uint64_t generation) {
    if(!native || !queue || impl_ || !generation || data.meshlets.empty() || data.bounds.size() != data.meshlets.size() ||
       data.vertices.empty() || data.primitives.empty())
        return std::unexpected(Error(ErrorCategory::Resource, 81, "Invalid meshlet upload state."));
    auto* device = static_cast<ID3D12Device*>(native);
    auto next = std::make_unique<Impl>();
    next->generation = generation; next->ranges = data.submeshes;
    const std::array bytes {std::as_bytes(std::span(data.meshlets)), std::as_bytes(std::span(data.vertices)),
                            std::as_bytes(std::span(data.primitives)), std::as_bytes(std::span(data.bounds))};
    for(const auto& m : data.meshlets) {
        if(m.vertexCount == 0 || m.vertexCount > NEXUS_MESHLET_VERTICES || m.primitiveCount == 0 || m.primitiveCount > NEXUS_MESHLET_PRIMITIVES ||
           uint64_t(m.vertexOffset) + m.vertexCount > data.vertices.size() || uint64_t(m.primitiveOffset) + m.primitiveCount > data.primitives.size())
            return std::unexpected(Error(ErrorCategory::Resource, 81, "Invalid meshlet buffer range."));
        for(uint32_t i = 0; i < m.primitiveCount; ++i) {
            const uint32_t packed = data.primitives[m.primitiveOffset + i];
            if((packed & 255U) >= m.vertexCount || ((packed >> 8) & 255U) >= m.vertexCount || ((packed >> 16) & 255U) >= m.vertexCount)
                return std::unexpected(Error(ErrorCategory::Resource, 81, "Invalid meshlet primitive index."));
        }
    }
    for(const auto& range : data.submeshes) if(uint64_t(range.first) + range.count > data.meshlets.size())
        return std::unexpected(Error(ErrorCategory::Resource, 81, "Invalid meshlet submesh range."));
    for(const auto& bound : data.bounds) {
        if(!std::isfinite(bound.radius) || bound.radius < 0) return std::unexpected(Error(ErrorCategory::Resource, 81, "Invalid meshlet sphere radius."));
        for(float component : bound.center) if(!std::isfinite(component)) return std::unexpected(Error(ErrorCategory::Resource, 81, "Invalid meshlet sphere center."));
    }
    next->upload = std::make_unique<ImmediateUploadContext>();
    auto& upload = *next->upload;
    auto& staging = next->staging;
    auto status = upload.Initialize(device);
    if(!status) return status;
    ////////////////////////////////////////////////////////////////////////////
    // 全 Meshlet Buffer を1つの Upload List で転送。staging は Fence 完了まで生存。
    ////////////////////////////////////////////////////////////////////////////
    for(size_t i = 0; i < bytes.size(); ++i) {
        D3D12_RESOURCE_DESC desc {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; desc.Width = bytes[i].size();
        desc.Height = 1; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
        desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES heap {}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        HRESULT hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&next->buffers[i]));
        if(FAILED(hr)) return std::unexpected(MakeDirectXError(81, hr, "Meshlet default buffer creation failed."));
        heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&staging[i]));
        if(FAILED(hr)) return std::unexpected(MakeDirectXError(81, hr, "Meshlet staging creation failed."));
        void* mapped = nullptr; const D3D12_RANGE read {0, 0};
        hr = staging[i]->Map(0, &read, &mapped);
        if(FAILED(hr)) return std::unexpected(MakeDirectXError(81, hr, "Meshlet staging map failed."));
        std::memcpy(mapped, bytes[i].data(), bytes[i].size()); staging[i]->Unmap(0, nullptr);
        upload.GetCommandList()->CopyBufferRegion(next->buffers[i].Get(), 0, staging[i].Get(), 0, bytes[i].size());
        D3D12_RESOURCE_BARRIER barrier {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition = {next->buffers[i].Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                              D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
        upload.GetCommandList()->ResourceBarrier(1, &barrier);
    }
    status = upload.ExecuteAndWait(queue);
    if(!status) {
        // Signal / Wait 失敗時は投入済みかもしれない。GPU idle 後の Shutdown まで
        // staging / destination / allocator を保持し、描画用としては公開しない。
        next->generation = 0; next->ranges.clear(); impl_ = std::move(next);
        return status;
    }
    for(auto& resource : staging) resource.Reset();
    next->upload.reset();
    impl_ = std::move(next);
    return {};
}
}
