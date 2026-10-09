#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <d3d12.h>
#include <wrl/client.h>
#include "Foundation/Error/Result.h"
#include "Graphics/PostProcess/PostProcessSettings.h"

namespace NexusEngine {
    // Direct queue owns ordering. Resize/destruction require GPU idle.
    class PostProcessBackend final {
    public:
        /** Create shader pipelines and scene/bloom targets. No GPU work is submitted. */
        [[nodiscard]] Result<void> Initialize(ID3D12Device*, const std::filesystem::path&, uint32_t, uint32_t);
        /** Atomically replace HDR resources; caller guarantees GPU idle. */
        [[nodiscard]] Result<void> Resize(ID3D12Device*, uint32_t, uint32_t);
        /** Release only after the direct queue is idle. */
        void Shutdown() noexcept;
        /** Transition and clear the scene-linear HDR target before any scene draws. */
        void BeginScene(ID3D12GraphicsCommandList*, D3D12_CPU_DESCRIPTOR_HANDLE depth, const float* clear);
        /** Record bloom and SDR display conversion, leaving output in render-target state. */
        void Resolve(ID3D12GraphicsCommandList*, D3D12_CPU_DESCRIPTOR_HANDLE output, const PostProcessSettings&);
        [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE GetSceneRtv() const noexcept;
    private:
        struct Target {
            Microsoft::WRL::ComPtr<ID3D12Resource> resource;
            uint32_t width = 0, height = 0;
        };
        // Scene HDR, five bloom levels, then full-resolution display colour for FXAA.
        std::array<Target, 7> targets_;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_, srvHeap_;
        Microsoft::WRL::ComPtr<ID3D12RootSignature> root_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> downsample_, composite_, antiAliasing_;
        uint32_t rtvStride_ = 0;
        [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE Rtv(uint32_t) const noexcept;
    };
}
