#pragma once
#include "Graphics/Descriptor/DescriptorAllocator.h"
#include "Graphics/Viewport/ViewportImage.h"
#include <d3d12.h>
#include <wrl/client.h>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * ViewportTarget
 * - SwapChainと独立したSDR RenderTargetとRTV/SRVを所有する
 * - 同一Direct Queue上でScene出力→UI samplingを順序付けるローカル描画用Backend
 *---------------------------------------------------------------------------------------*/
class ViewportTarget final {
public:
    ~ViewportTarget() noexcept { Shutdown(); }
    ViewportTarget() = default;
    ViewportTarget(const ViewportTarget&) = delete;
    ViewportTarget& operator=(const ViewportTarget&) = delete;
    /** \brief GPU idle保証後に画像を作成・置換する。失敗時は旧画像を保持する */
    Result<void> Resize(ID3D12Device* device, DescriptorAllocator& resources, DescriptorAllocator& renderTargets,
        uint32_t width, uint32_t height);
    /** \brief 同じqueue上のsamplingからRenderTarget書込みへ遷移する */
    void BeginWrite(ID3D12GraphicsCommandList* commands) const noexcept;
    /** \brief Scene出力完了後にUI sampling可能な状態へ戻す */
    void EndWrite(ID3D12GraphicsCommandList* commands) const noexcept;
    /** \brief GPU idle保証後、allocatorより先に解放する */
    void Shutdown() noexcept;
    D3D12_CPU_DESCRIPTOR_HANDLE Rtv() const noexcept { return rtv_.cpuHandle; }
    ViewportImage Image() const noexcept { return {static_cast<uintptr_t>(srv_.gpuHandle.ptr), width_, height_}; }
private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_; //< GPU完了まで保持する出力画像
    DescriptorAllocator* resources_ = nullptr; //< GraphicsSystem所有
    DescriptorAllocator* renderTargets_ = nullptr; //< GraphicsSystem所有
    DescriptorHandle srv_, rtv_; //< Resizeでもslotを維持し、idle後にViewを書き換える
    uint32_t width_ = 0, height_ = 0; //< 確保済み解像度
};
}
