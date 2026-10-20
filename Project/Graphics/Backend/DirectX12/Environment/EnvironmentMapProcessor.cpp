#include "EnvironmentMapProcessor.h"
#include <algorithm>
#include <cstring>
#include <utility>
#include "Graphics/Core/CommandQueue.h"
#include "Graphics/Descriptor/BindlessDescriptorTable.h"
#include "Graphics/Resource/HdrImage.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Foundation/Logging/Logger.h"
namespace NexusEngine {
    namespace {
        constexpr uint32_t kCubeSize = 256, kCubeMips = 9, kIrradianceSize = 32, kLutSize = 128;
        constexpr auto kRead = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
        void Transition(ID3D12GraphicsCommandList* list, ID3D12Resource* resource,
                        D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after, uint32_t subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) {
            D3D12_RESOURCE_BARRIER b {}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition = {resource, subresource, before, after}; list->ResourceBarrier(1, &b);
        }
        struct Constants {
            uint32_t source, output, size, samples;
            float roughness;
            uint32_t sourceSize, sourceMip, reserved = 0;
        };
        static_assert(sizeof(Constants) == 32);
    }
    void EnvironmentMapProcessor::Initialize(void* device, CommandQueue* queue, BindlessDescriptorTable* table, const std::filesystem::path& shaders) {
        device_ = static_cast<ID3D12Device*>(device); queue_ = queue; table_ = table; shaders_ = shaders;
    }
    Result<void> EnvironmentMapProcessor::CreatePipeline() {
        if(root_) return {};
        D3D12_ROOT_PARAMETER parameter {}; parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameter.Constants = {0, 0, 8};
        D3D12_STATIC_SAMPLER_DESC samplers[2] {};
        for(uint32_t i=0;i<2;++i) {
            auto& s = samplers[i]; s.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            s.AddressU = i ? D3D12_TEXTURE_ADDRESS_MODE_WRAP : D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            s.AddressV = s.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            s.MaxLOD = D3D12_FLOAT32_MAX; s.MaxAnisotropy = 1;
            s.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER; s.ShaderRegister = i;
        }
        D3D12_ROOT_SIGNATURE_DESC desc {1, &parameter, 2, samplers,
            D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED};
        Microsoft::WRL::ComPtr<ID3DBlob> blob, errors;
        auto hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
        if(FAILED(hr)) return std::unexpected(MakeDirectXError(90, hr, "IBL root serialization failed."));
        Microsoft::WRL::ComPtr<ID3D12RootSignature> root;
        hr = device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root));
        if(FAILED(hr)) return std::unexpected(MakeDirectXError(90, hr, "IBL root creation failed."));
        const wchar_t* entries[] = {L"Convert", L"Downsample", L"Irradiance", L"Prefilter", L"IntegrateBRDF"};
        for(uint32_t i=0;i<5;++i) {
            auto code = ShaderCompiler{}.Compile({shaders_ / L"Environment/EnvironmentMap.CS.hlsl", entries[i], L"cs_6_6"});
            if(!code) return std::unexpected(std::move(code.error()));
            D3D12_COMPUTE_PIPELINE_STATE_DESC pso {}; pso.pRootSignature = root.Get(); pso.CS = {code->data(), code->size()};
            hr = device_->CreateComputePipelineState(&pso, IID_PPV_ARGS(&pipelines_[i]));
            if(FAILED(hr)) return std::unexpected(MakeDirectXError(90, hr, "IBL compute PSO creation failed."));
        }
        root_ = std::move(root); return {};
    }
    void EnvironmentMapProcessor::Release(Map& map) noexcept {
        if(table_ && queue_) for(auto ref : map.descriptors) {
            if(!ref.IsValid()) continue;
            auto status = table_->Retire(ref, queue_->GetCompletedFenceValue());
            if(!status) NEXUS_LOG_ERROR("Environment", status.error().GetMessageText());
        }
        map = {};
        if(table_ && queue_) table_->CollectGarbage(queue_->GetCompletedFenceValue());
    }
    void EnvironmentMapProcessor::Shutdown() noexcept {
        // Caller guarantees idle, including any failed submission retained in pending_.
        Release(active_); Release(pending_);
        if(table_ && queue_) {
            for(auto ref : {lutView_, lutUav_, sampler_}) if(ref.IsValid()) {
                auto status = table_->Retire(ref, queue_->GetCompletedFenceValue());
                if(!status) NEXUS_LOG_ERROR("Environment", status.error().GetMessageText());
            }
            table_->CollectGarbage(queue_->GetCompletedFenceValue());
        }
        lutView_ = {}; lutUav_ = {}; sampler_ = {};
        lut_.Reset(); source_.Reset(); upload_.Reset(); uploadContext_ = {};
        for(auto& pso : pipelines_) pso.Reset(); root_.Reset();
        device_ = nullptr; queue_ = nullptr; table_ = nullptr; ready_ = poisoned_ = false;
    }
    EnvironmentDrawData EnvironmentMapProcessor::GetDrawData() const noexcept {
        EnvironmentDrawData data;
        if(!active_.textures[0]) return data;
        data.textures = {active_.views[0].index, active_.views[1].index, active_.views[2].index, lutView_.index};
        data.parameters[3] = float(kCubeMips - 1); data.options = {sampler_.index, 1, 0, 0};
        return data;
    }
    Result<void> EnvironmentMapProcessor::Bake(const HdrImage& image) {
        if(!device_ || !queue_ || !table_ || poisoned_ || image.pixels.size() != size_t(image.width) * image.height || !image.width || !image.height)
            return std::unexpected(Error(ErrorCategory::Graphics, 90, "IBL dependencies/image invalid or previous submission failed."));
        auto status = CreatePipeline(); if(!status) return status;
        // Reload is rare. Waiting here permits simple bounded resource ownership without
        // introducing frame-by-frame queues or retaining every previous HDR generation.
        status = queue_->WaitForIdle(); if(!status) return status;
        Release(pending_); source_.Reset(); upload_.Reset(); uploadContext_ = {};
        auto makeTexture = [&](Resource& out, uint32_t sizeX, uint32_t sizeY, uint16_t layers, uint16_t mips, DXGI_FORMAT format, bool uav) -> Result<void> {
            D3D12_RESOURCE_DESC d {}; d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            d.Width=sizeX; d.Height=sizeY; d.DepthOrArraySize=layers; d.MipLevels=mips;
            d.Format=format; d.SampleDesc.Count=1;
            d.Flags=uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
            D3D12_HEAP_PROPERTIES heap {}; heap.Type=D3D12_HEAP_TYPE_DEFAULT;
            auto hr=device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,
                uav ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&out));
            if(FAILED(hr)) return std::unexpected(MakeDirectXError(90,hr,"IBL texture allocation failed."));
            return {};
        };
        auto srv = [&](Resource& resource, bool cube, uint32_t first, uint32_t count) -> Result<ShaderResourceRef> {
            D3D12_SHADER_RESOURCE_VIEW_DESC d {}; d.Format=resource->GetDesc().Format;
            d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            if(cube) { d.ViewDimension=D3D12_SRV_DIMENSION_TEXTURECUBE; d.TextureCube.MostDetailedMip=first; d.TextureCube.MipLevels=count; }
            else { d.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D; d.Texture2D.MipLevels=count; }
            return table_->CreateShaderResourceView(resource.Get(),d);
        };
        auto uav = [&](Resource& resource, bool cube, uint32_t mip) -> Result<ShaderResourceRef> {
            D3D12_UNORDERED_ACCESS_VIEW_DESC d {}; d.Format=resource->GetDesc().Format;
            if(cube) { d.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2DARRAY; d.Texture2DArray.MipSlice=mip; d.Texture2DArray.ArraySize=6; }
            else { d.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D; }
            return table_->CreateUnorderedAccessView(resource.Get(),nullptr,d);
        };
        status = makeTexture(source_, image.width,image.height,1,1,DXGI_FORMAT_R32G32B32A32_FLOAT,false); if(!status) return status;
        // Upload only float pixels, never encoded bytes. Driver footprints supply row alignment.
        auto sourceDesc=source_->GetDesc(); D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp {}; UINT64 bytes=0;
        device_->GetCopyableFootprints(&sourceDesc,0,1,0,&fp,nullptr,nullptr,&bytes);
        D3D12_RESOURCE_DESC bd {}; bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width=bytes;
        bd.Height=1; bd.DepthOrArraySize=1; bd.MipLevels=1; bd.SampleDesc.Count=1; bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES heap {}; heap.Type=D3D12_HEAP_TYPE_UPLOAD;
        auto hr=device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload_));
        if(FAILED(hr)) return std::unexpected(MakeDirectXError(90,hr,"HDR upload allocation failed."));
        void* mapped=nullptr; D3D12_RANGE empty {0,0}; hr=upload_->Map(0,&empty,&mapped);
        if(FAILED(hr)) return std::unexpected(MakeDirectXError(90,hr,"HDR upload Map failed."));
        for(uint32_t y=0;y<image.height;++y) std::memcpy(static_cast<uint8_t*>(mapped)+fp.Offset+size_t(y)*fp.Footprint.RowPitch,
            image.pixels.data()+size_t(y)*image.width,size_t(image.width)*16);
        upload_->Unmap(0,nullptr);
        status = uploadContext_.Initialize(device_); if(!status) return status;
        auto* list=uploadContext_.GetCommandList();
        D3D12_TEXTURE_COPY_LOCATION dst {}; dst.pResource=source_.Get(); dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION src {}; src.pResource=upload_.Get(); src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint=fp;
        list->CopyTextureRegion(&dst,0,0,0,&src,nullptr); Transition(list,source_.Get(),D3D12_RESOURCE_STATE_COPY_DEST,kRead);
        auto sourceView=srv(source_,false,0,1); if(!sourceView) return std::unexpected(std::move(sourceView.error()));
        pending_.descriptors.push_back(*sourceView);
        for(uint32_t i=0;i<3;++i) {
            status=makeTexture(pending_.textures[i],i==1?kIrradianceSize:kCubeSize,i==1?kIrradianceSize:kCubeSize,6,i==1?1:kCubeMips,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
            if(!status) return status;
            auto view=srv(pending_.textures[i],true,0,i==1?1:kCubeMips);
            if(!view) return std::unexpected(std::move(view.error()));
            pending_.views[i]=*view; pending_.descriptors.push_back(*view);
        }
        if(!sampler_.IsValid()) {
            D3D12_SAMPLER_DESC d {}; d.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            d.AddressU=d.AddressV=d.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            d.MaxLOD=D3D12_FLOAT32_MAX; d.MaxAnisotropy=1; d.ComparisonFunc=D3D12_COMPARISON_FUNC_NEVER;
            auto ref=table_->CreateSampler(d); if(!ref) return std::unexpected(std::move(ref.error())); sampler_=*ref;
        }
        if(!lut_) {
            status=makeTexture(lut_,kLutSize,kLutSize,1,1,DXGI_FORMAT_R16G16_FLOAT,true); if(!status) return status;
        }
        if(!lutView_.IsValid()) { auto ref=srv(lut_,false,0,1); if(!ref) return std::unexpected(std::move(ref.error())); lutView_=*ref; }
        if(!lutUav_.IsValid()) { auto ref=uav(lut_,false,0); if(!ref) return std::unexpected(std::move(ref.error())); lutUav_=*ref; }
        table_->Bind(list); list->SetComputeRootSignature(root_.Get());
        auto dispatch = [&](uint32_t pipeline, Constants constants, uint32_t faces) {
            list->SetPipelineState(pipelines_[pipeline].Get()); list->SetComputeRoot32BitConstants(0,8,&constants,0);
            list->Dispatch((constants.size+7)/8,(constants.size+7)/8,faces);
        };
        // Only the written mip transitions to SRV. Downsample reads a one-mip view,
        // so no SRV range overlaps a different mip still in UAV state.
        for(uint32_t mip=0;mip<kCubeMips;++mip) {
            auto out=uav(pending_.textures[0],true,mip); if(!out) return std::unexpected(std::move(out.error())); pending_.descriptors.push_back(*out);
            uint32_t input=sourceView->index;
            if(mip) {
                auto view=srv(pending_.textures[0],true,mip-1,1); if(!view) return std::unexpected(std::move(view.error()));
                input=view->index; pending_.descriptors.push_back(*view);
            }
            dispatch(mip?1:0,{input,out->index,kCubeSize>>mip,1,0,kCubeSize,0},6);
            for(uint32_t face=0;face<6;++face) Transition(list,pending_.textures[0].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,kRead,mip+face*kCubeMips);
        }
        for(uint32_t i=1;i<3;++i) for(uint32_t mip=0;mip<(i==1?1:kCubeMips);++mip) {
            auto out=uav(pending_.textures[i],true,mip); if(!out) return std::unexpected(std::move(out.error())); pending_.descriptors.push_back(*out);
            dispatch(i==1?2:3,{pending_.views[0].index,out->index,i==1?kIrradianceSize:kCubeSize>>mip,512,float(mip)/(kCubeMips-1),kCubeSize,0},6);
        }
        Transition(list,pending_.textures[1].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,kRead);
        Transition(list,pending_.textures[2].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,kRead);
        if(!ready_) {
            dispatch(4,{0,lutUav_.index,kLutSize,1024,0,0,0},1);
            Transition(list,lut_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,kRead);
        }
        // Keep every submitted object on this owner until the fence is confirmed.
        status=uploadContext_.ExecuteAndWait(queue_);
        if(!status) { poisoned_=true; return status; }
        ready_=true;
        // Free transient views now; only the three sampled cube SRVs survive reload.
        for(auto& ref:pending_.descriptors) {
            if(std::any_of(pending_.views.begin(),pending_.views.end(),[&](auto view){ return view.index == ref.index && view.generation == ref.generation; })) continue;
            auto retired=table_->Retire(ref,queue_->GetCompletedFenceValue());
            if(!retired) return retired;
            ref = {}; // A later failure must not retire this view twice.
        }
        pending_.descriptors.assign(pending_.views.begin(),pending_.views.end());
        Release(active_); active_=std::move(pending_); pending_={};
        source_.Reset(); upload_.Reset(); uploadContext_={}; table_->CollectGarbage(queue_->GetCompletedFenceValue());
        return {};
    }
}
