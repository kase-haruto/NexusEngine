#include "PostProcessBackend.h"
#include <algorithm>
#include <utility>
#include "Graphics/Shader/ShaderCompiler.h"

namespace NexusEngine {
    namespace {
        void Transition(ID3D12GraphicsCommandList* commands, ID3D12Resource* resource,
                        D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
            D3D12_RESOURCE_BARRIER b {};
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
            commands->ResourceBarrier(1, &b);
        }
        void Viewport(ID3D12GraphicsCommandList* commands, uint32_t width, uint32_t height) {
            const D3D12_VIEWPORT v {0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
            const D3D12_RECT s {0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
            commands->RSSetViewports(1, &v); commands->RSSetScissorRects(1, &s);
        }
        struct Constants {
            uint32_t source = 0; float exposure, intensity, threshold;
            float knee, saturation, contrast, gamma;
            std::array<float, 4> gain, offset, balance;
            uint32_t toneMapper; float padding[3] {};
        };
        static_assert(sizeof(Constants) == 24 * sizeof(uint32_t));
    }
    D3D12_CPU_DESCRIPTOR_HANDLE PostProcessBackend::Rtv(uint32_t i) const noexcept {
        auto handle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(i) * rtvStride_; return handle;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE PostProcessBackend::GetSceneRtv() const noexcept { return Rtv(0); }
    void PostProcessBackend::Shutdown() noexcept {
        for(auto& target : targets_) target = {};
        downsample_.Reset(); composite_.Reset(); antiAliasing_.Reset(); root_.Reset();
        srvHeap_.Reset(); rtvHeap_.Reset(); rtvStride_ = 0;
    }

    Result<void> PostProcessBackend::Initialize(ID3D12Device* device, const std::filesystem::path& directory,
                                               uint32_t width, uint32_t height) {
        ShaderCompiler compiler;
        auto vs = compiler.Compile({directory / L"PostProcess/PostProcess.hlsl", L"VSMain", L"vs_6_0"});
        if(!vs) return std::unexpected(std::move(vs.error()));
        auto down = compiler.Compile({directory / L"PostProcess/PostProcess.hlsl", L"Downsample", L"ps_6_0"});
        if(!down) return std::unexpected(std::move(down.error()));
        auto composite = compiler.Compile({directory / L"PostProcess/PostProcess.hlsl", L"Composite", L"ps_6_0"});
        if(!composite) return std::unexpected(std::move(composite.error()));
        auto aa = compiler.Compile({directory / L"PostProcess/PostProcess.hlsl", L"AntiAliasing", L"ps_6_0"});
        if(!aa) return std::unexpected(std::move(aa.error()));
        D3D12_DESCRIPTOR_RANGE range {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 7, 0, 0, 0};
        D3D12_ROOT_PARAMETER parameters[2] {};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[0].DescriptorTable = {1, &range};
        parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[1].Constants = {0, 0, 24};
        parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC sampler {};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.MaxAnisotropy = 1; sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.MaxLOD = D3D12_FLOAT32_MAX; sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC rootDesc {2, parameters, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
        Microsoft::WRL::ComPtr<ID3DBlob> serialized, errors;
        auto hr = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors);
        if(FAILED(hr)) return std::unexpected(MakeDirectXError(80, hr, "Post-process root signature serialization failed."));
        hr = device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&root_));
        if(FAILED(hr)) return std::unexpected(MakeDirectXError(81, hr, "Post-process root signature creation failed."));
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso {};
        pso.pRootSignature = root_.Get(); pso.VS = {vs->data(), vs->size()};
        for(auto& blend : pso.BlendState.RenderTarget)
            blend = {FALSE,FALSE,D3D12_BLEND_ONE,D3D12_BLEND_ZERO,D3D12_BLEND_OP_ADD,D3D12_BLEND_ONE,D3D12_BLEND_ZERO,D3D12_BLEND_OP_ADD,D3D12_LOGIC_OP_NOOP,D3D12_COLOR_WRITE_ENABLE_ALL};
        pso.SampleMask = UINT_MAX;
        pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pso.RasterizerState.DepthClipEnable = TRUE;
        pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets = 1; pso.SampleDesc.Count = 1;
        pso.PS = {down->data(), down->size()}; pso.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        hr = device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&downsample_));
        if(FAILED(hr)) return std::unexpected(MakeDirectXError(82, hr, "Bloom pipeline creation failed."));
        pso.PS = {composite->data(), composite->size()}; pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        hr = device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&composite_));
        if(FAILED(hr)) return std::unexpected(MakeDirectXError(83, hr, "Tone mapping pipeline creation failed."));
        pso.PS = {aa->data(), aa->size()};
        hr = device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&antiAliasing_));
        if(FAILED(hr)) return std::unexpected(MakeDirectXError(88, hr, "Anti-aliasing pipeline creation failed."));
        return Resize(device, width, height);
    }
    Result<void> PostProcessBackend::Resize(ID3D12Device* device, uint32_t width, uint32_t height) {
        if(width == 0 || height == 0)
            return std::unexpected(Error(ErrorCategory::Graphics, 84, "Post-process dimensions must be nonzero."));
        // Build a complete replacement before releasing the previous resources.
        std::array<Target, 7> replacement;
        const auto displayWidth = width, displayHeight = height;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv, srv;
        D3D12_DESCRIPTOR_HEAP_DESC heap {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 7, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
        auto hr = device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&rtv));
        if(FAILED(hr)) return std::unexpected(MakeDirectXError(85, hr, "Post-process RTV heap creation failed."));
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr = device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&srv));
        if(FAILED(hr)) return std::unexpected(MakeDirectXError(86, hr, "Post-process SRV heap creation failed."));
        const auto rtvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        const auto srvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        auto r = rtv->GetCPUDescriptorHandleForHeapStart(); auto s = srv->GetCPUDescriptorHandleForHeapStart();
        D3D12_HEAP_PROPERTIES properties {}; properties.Type = D3D12_HEAP_TYPE_DEFAULT;
        for(auto& target : replacement) {
            const bool display = &target == &replacement[6];
            if(display) { width = displayWidth; height = displayHeight; }
            target.width = width; target.height = height;
            D3D12_RESOURCE_DESC desc {};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; desc.Width = width; desc.Height = height;
            desc.DepthOrArraySize = 1; desc.MipLevels = 1; desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            if(display) desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1; desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            hr = device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&target.resource));
            if(FAILED(hr)) return std::unexpected(MakeDirectXError(87, hr, "HDR target creation failed."));
            device->CreateRenderTargetView(target.resource.Get(), nullptr, r);
            D3D12_SHADER_RESOURCE_VIEW_DESC view {};
            view.Format = desc.Format; view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; view.Texture2D.MipLevels = 1;
            device->CreateShaderResourceView(target.resource.Get(), &view, s);
            r.ptr += rtvStride; s.ptr += srvStride;
            width = (std::max)(1u, width / 2); height = (std::max)(1u, height / 2);
        }
        targets_ = std::move(replacement); rtvHeap_ = std::move(rtv); srvHeap_ = std::move(srv); rtvStride_ = rtvStride;
        return {};
    }
    void PostProcessBackend::BeginScene(ID3D12GraphicsCommandList* commands, D3D12_CPU_DESCRIPTOR_HANDLE depth, const float* clear) {
        Transition(commands, targets_[0].resource.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        auto rtv = Rtv(0); commands->OMSetRenderTargets(1, &rtv, FALSE, &depth);
        // Clear colour uses the same finite half-float contract as scene shader output.
        std::array<float, 4> hdrClear {};
        for(size_t i = 0; i < hdrClear.size(); ++i)
            hdrClear[i] = std::isfinite(clear[i]) ? std::clamp(clear[i], 0.0f, 65504.0f) : 0.0f;
        commands->ClearRenderTargetView(rtv, hdrClear.data(), 0, nullptr);
    }
    void PostProcessBackend::Resolve(ID3D12GraphicsCommandList* commands, D3D12_CPU_DESCRIPTOR_HANDLE output, const PostProcessSettings& settings) {
        Transition(commands, targets_[0].resource.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        auto* heap = srvHeap_.Get(); commands->SetDescriptorHeaps(1, &heap);
        commands->SetGraphicsRootSignature(root_.Get());
        commands->SetGraphicsRootDescriptorTable(0, srvHeap_->GetGPUDescriptorHandleForHeapStart());
        commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        Constants c {};
        c.exposure = std::exp2(settings.exposureEV); c.intensity = settings.bloomEnabled ? settings.bloomIntensity : 0;
        c.threshold = settings.bloomThreshold; c.knee = settings.bloomSoftKnee;
        c.saturation = settings.saturation; c.contrast = settings.contrast; c.gamma = settings.gamma;
        for(int i = 0; i < 3; ++i) { c.gain[i] = settings.gain[i]; c.offset[i] = settings.offset[i]; c.balance[i] = settings.whiteBalance[i]; }
        c.toneMapper = static_cast<uint32_t>(settings.GetEffectiveToneMapper());
        // All targets return to SRV state after each frame, independent of settings.
        if(c.intensity > 0) {
            commands->SetPipelineState(downsample_.Get());
            for(uint32_t i = 1; i < 6; ++i) {
                Transition(commands, targets_[i].resource.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
                const auto rtv = Rtv(i); commands->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
                Viewport(commands, targets_[i].width, targets_[i].height);
                c.source = i - 1; commands->SetGraphicsRoot32BitConstants(1, 24, &c, 0);
                commands->DrawInstanced(3, 1, 0, 0);
                Transition(commands, targets_[i].resource.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            }
        }
        commands->SetPipelineState(composite_.Get());
        if(settings.antiAliasingEnabled)
            Transition(commands, targets_[6].resource.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        const auto compositeOutput = settings.antiAliasingEnabled ? Rtv(6) : output;
        commands->OMSetRenderTargets(1, &compositeOutput, FALSE, nullptr);
        Viewport(commands, targets_[0].width, targets_[0].height);
        commands->SetGraphicsRoot32BitConstants(1, 24, &c, 0); commands->DrawInstanced(3, 1, 0, 0);
        if(settings.antiAliasingEnabled) {
            Transition(commands, targets_[6].resource.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            commands->SetPipelineState(antiAliasing_.Get());
            commands->OMSetRenderTargets(1, &output, FALSE, nullptr);
            commands->DrawInstanced(3, 1, 0, 0);
        }
    }
}
