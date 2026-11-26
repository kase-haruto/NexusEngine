#include "RenderCaptureService.h"
#include <cstring>
#include <limits>
#include <utility>

namespace NexusEngine {
////////////////////////////////////////////////////////////////////////////////
// SDR出力を同じCommand Listで転送し、その後のUI samplingと順序付ける
////////////////////////////////////////////////////////////////////////////////
bool RenderCaptureService::Record(ID3D12Device& device, ID3D12GraphicsCommandList& commands,
    ID3D12Resource& source, D3D12_RESOURCE_STATES state) {
    const auto desc = source.GetDesc();
    if(Busy() || desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM || desc.SampleDesc.Count != 1) {
        return false;
    }

    device.GetCopyableFootprints(&desc, 0, 1, 0, &footprint_, nullptr, nullptr, &bytes_);
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    heap.CreationNodeMask = heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = bytes_;
    buffer.Height = 1;
    buffer.DepthOrArraySize = 1;
    buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if(FAILED(device.CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback_)))) {
        return false;
    }

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {&source, 0, state, D3D12_RESOURCE_STATE_COPY_SOURCE};
    D3D12_TEXTURE_COPY_LOCATION from{&source, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    D3D12_TEXTURE_COPY_LOCATION to{readback_.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
    to.PlacedFootprint = footprint_;
    commands.ResourceBarrier(1, &barrier);
    commands.CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.StateAfter = state;
    commands.ResourceBarrier(1, &barrier);
    return true;
}

////////////////////////////////////////////////////////////////////////////////
// 他のFrameで完了を確認できるように投入位置を保存する
////////////////////////////////////////////////////////////////////////////////
void RenderCaptureService::Submitted(uint64_t fence) noexcept {
    fence_ = fence;
}

////////////////////////////////////////////////////////////////////////////////
// GPUのRowPitchとCPU側の密配置の差を吸収する
////////////////////////////////////////////////////////////////////////////////
std::optional<DecodedImage> RenderCaptureService::Collect(uint64_t completed) {
    if(!Busy() || !fence_ || completed < fence_) {
        return {};
    }
    auto buffer = std::move(readback_);
    fence_ = 0;
    if(completed == (std::numeric_limits<uint64_t>::max)()) {
        return DecodedImage{}; // Device removed
    }
    const auto width = footprint_.Footprint.Width;
    const auto height = footprint_.Footprint.Height;
    const size_t row = static_cast<size_t>(width) * 4;
    DecodedImage image{width, height, std::vector<uint8_t>(row * height)};
    void* mapped = nullptr;
    const D3D12_RANGE read{static_cast<SIZE_T>(footprint_.Offset), static_cast<SIZE_T>(bytes_)};
    if(FAILED(buffer->Map(0, &read, &mapped))) {
        return DecodedImage{};
    }
    const auto* start = static_cast<const uint8_t*>(mapped) + footprint_.Offset;
    for(uint32_t y = 0; y < height; ++y) {
        std::memcpy(image.rgbaPixels.data() + y * row, start + static_cast<size_t>(y) * footprint_.Footprint.RowPitch, row);
    }
    const D3D12_RANGE written{0, 0};
    buffer->Unmap(0, &written);
    return image;
}

////////////////////////////////////////////////////////////////////////////////
// コピー完了まで新しい要求によるReadbackの置換を許可しない
////////////////////////////////////////////////////////////////////////////////
bool RenderCaptureService::Busy() const noexcept {
    return readback_.Get() != nullptr;
}

////////////////////////////////////////////////////////////////////////////////
// 異常終了や保存期限超過でも、Deviceの終了より先にReadbackを解放する
////////////////////////////////////////////////////////////////////////////////
void RenderCaptureService::ReleaseAfterIdle() noexcept {
    readback_.Reset();
    fence_ = 0;
}
}
