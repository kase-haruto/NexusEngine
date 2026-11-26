#pragma once
#include "Graphics/Resource/WicImageDecoder.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <optional>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * RenderCaptureService
 * - 同一Direct QueueのFenceでSDR RenderTargetのReadbackを管理する
 *---------------------------------------------------------------------------------------*/
class RenderCaptureService final {
public:
    /** \brief SDR画像をコピーし、元のResource Stateを復元する */
    bool Record(ID3D12Device& device, ID3D12GraphicsCommandList& commands,
        ID3D12Resource& source, D3D12_RESOURCE_STATES state);
    /** \brief コピーを投入したFrameのFence値を関連付ける */
    void Submitted(uint64_t fence) noexcept;
    /** \brief Fence完了後にだけMapし、密配置RGBA8画像を返す */
    std::optional<DecodedImage> Collect(uint64_t completed);
    /** \brief 未投入または処理中のコピーがあるか */
    bool Busy() const noexcept;
    /** \brief Queueがidleになった後に未回収のReadbackを破棄する */
    void ReleaseAfterIdle() noexcept;
private:
    Microsoft::WRL::ComPtr<ID3D12Resource> readback_; //< Fence完了まで保持する転送先
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint_{}; //< GPUの行境界と解像度
    uint64_t fence_ = 0; //< このコピーが含まれるFrameのFence
    uint64_t bytes_ = 0; //< Map時にCPUが参照する範囲
};
}
