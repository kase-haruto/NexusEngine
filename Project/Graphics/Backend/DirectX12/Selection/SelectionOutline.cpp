#include "Graphics/Selection/SelectionOutline.h"
#include "Graphics/Renderer/GraphicsResourceFactory.h"
#include "Graphics/Renderer/GraphicsContext.h"
#include "Graphics/Descriptor/DescriptorManager.h"
#include "Graphics/Descriptor/BindlessDescriptorTable.h"
#include "Graphics/Model/ModelAssetData.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <cstddef>

namespace NexusEngine {
namespace {
// 共通RootSignatureはALL stageのSRV tableを使うため、両方のread stateを満たす。
const auto kMaskReadState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
}
class SelectionOutline::Impl final {
public:
    GraphicsResourceFactory* resources = nullptr;
    ID3D12Device* device = nullptr;
    DescriptorManager* descriptors = nullptr;
    BindlessDescriptorTable* bindless = nullptr;
    GraphicsPipeline maskPipeline, compositePipeline;
    Microsoft::WRL::ComPtr<ID3D12Resource> mask;
    DescriptorHandle rtv;
    ShaderResourceRef srv;
    uint32_t width = 0, height = 0;

    Result<void> Resize(uint32_t newWidth, uint32_t newHeight) {
        if(mask && width == newWidth && height == newHeight) return {};
        // 古いMaskとRTVは前Frameが参照し得る。同じQueueの完了後にだけ置換する。
        auto idle = resources->WaitForIdle(); if(!idle) return idle;
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; desc.Width = newWidth; desc.Height = newHeight;
        desc.DepthOrArraySize = 1; desc.MipLevels = 1; desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1; desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_CLEAR_VALUE clear{}; clear.Format = desc.Format;
        Microsoft::WRL::ComPtr<ID3D12Resource> replacement;
        auto hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            kMaskReadState, &clear, IID_PPV_ARGS(&replacement));
        if(FAILED(hr)) return std::unexpected(MakeDirectXError(150, hr, "Selection mask allocation failed."));
        D3D12_SHADER_RESOURCE_VIEW_DESC view{}; view.Format = desc.Format;
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; view.Texture2D.MipLevels = 1;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        auto reference = bindless->CreateShaderResourceView(replacement.Get(), view);
        if(!reference) return std::unexpected(reference.error());
        if(srv.IsValid()) { auto retired = resources->RetirePersistentShaderResource(srv); if(!retired) return retired; }
        device->CreateRenderTargetView(replacement.Get(), nullptr, rtv.cpuHandle);
        mask = std::move(replacement); srv = *reference; width = newWidth; height = newHeight;
        return {};
    }
};
SelectionOutline::SelectionOutline() noexcept = default;
SelectionOutline::~SelectionOutline() noexcept { Shutdown(); }
Result<void> SelectionOutline::Initialize(GraphicsResourceFactory& resources, const std::filesystem::path& shaders) {
    if(impl_) return std::unexpected(Error(ErrorCategory::Graphics, 150, "Selection outline already initialized."));
    auto next = std::make_unique<Impl>();
    next->resources = &resources; next->device = static_cast<ID3D12Device*>(resources.nativeDevice_);
    next->descriptors = resources.descriptors_; next->bindless = resources.bindlessDescriptors_;
    if(!next->device || !next->descriptors || !next->bindless)
        return std::unexpected(Error(ErrorCategory::Graphics, 150, "Selection outline backend is unavailable."));
    Shader vertex, pixel;
    auto result = vertex.Initialize({shaders / "Primitive/Primitive.VS.hlsl", L"main", L"vs_6_6"}, ShaderStage::Vertex);
    if(!result) return result;
    result = pixel.Initialize({shaders / "Primitive/Selection.PS.hlsl", L"main", L"ps_6_6"}, ShaderStage::Pixel);
    if(!result) return result;
    GraphicsPipelineDesc desc;
    desc.doubleSided = true;
    desc.vertexLayout = {
        {"POSITION",0,VertexFormat::Float3,offsetof(ModelVertex,position)}, {"NORMAL",0,VertexFormat::Float3,offsetof(ModelVertex,normal)},
        {"TEXCOORD",0,VertexFormat::Float2,offsetof(ModelVertex,texcoord)}, {"COLOR",0,VertexFormat::Float4,offsetof(ModelVertex,color)},
        {"BLENDINDICES",0,VertexFormat::Float4,offsetof(ModelVertex,jointIndices)}, {"BLENDWEIGHT",0,VertexFormat::Float4,offsetof(ModelVertex,jointWeights)},
        {"TANGENT",0,VertexFormat::Float4,offsetof(ModelVertex,tangent)}, {"TEXCOORD",1,VertexFormat::Float2,offsetof(ModelVertex,lightmapUV)}};
    // DSVを接続せず、Depth Test/Writeともに無効なPSOを使う。
    result = resources.CreateGraphicsPipeline(next->maskPipeline, std::move(vertex), std::move(pixel), desc);
    if(!result) return result;
    Shader fullscreen, edge;
    result = fullscreen.Initialize({shaders / "Primitive/SelectionComposite.VS.hlsl", L"main", L"vs_6_6"}, ShaderStage::Vertex);
    if(!result) return result;
    result = edge.Initialize({shaders / "Primitive/SelectionComposite.PS.hlsl", L"main", L"ps_6_6"}, ShaderStage::Pixel);
    if(!result) return result;
    desc.vertexLayout.clear(); desc.hdrRenderTarget = true; desc.alphaBlending = true;
    result = resources.CreateGraphicsPipeline(next->compositePipeline, std::move(fullscreen), std::move(edge), desc);
    if(!result) return result;
    auto allocation = next->descriptors->RenderTargets().Allocate();
    if(!allocation) return std::unexpected(allocation.error());
    next->rtv = *allocation; impl_ = std::move(next); return {};
}
Result<void> SelectionOutline::Render(GraphicsContext& context, std::span<const SelectionOutlineDraw> draws) {
    if(draws.empty()) return {};
    if(!impl_) return std::unexpected(Error(ErrorCategory::Graphics, 150, "Selection outline is not initialized."));
    auto& state = *impl_;
    auto resized = state.Resize(context.GetRenderWidth(), context.GetRenderHeight()); if(!resized) return resized;
    auto* commands = static_cast<ID3D12GraphicsCommandList*>(context.GetNativeCommandList());
    D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {state.mask.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        kMaskReadState, D3D12_RESOURCE_STATE_RENDER_TARGET};
    commands->ResourceBarrier(1, &barrier);
    commands->OMSetRenderTargets(1, &state.rtv.cpuHandle, FALSE, nullptr);
    const float clear[4]{}; commands->ClearRenderTargetView(state.rtv.cpuHandle, clear, 0, nullptr);
    context.SetGraphicsPipeline(state.maskPipeline);
    context.SetPrimitiveTopology(PrimitiveTopology::TriangleList);
    Result<void> result;
    for(const auto& draw : draws) {
        context.SetMesh(*draw.mesh);
        result = context.SetGraphicsConstantBufferTable(state.maskPipeline, *draw.constants);
        if(!result) break;
        context.DrawIndexed(draw.submesh.indexCount, 1, draw.submesh.firstIndex, draw.submesh.vertexOffset);
    }
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter); commands->ResourceBarrier(1, &barrier);
    // 通常描画を全て終えた後で合成し、後続のMeshによる上書きも防ぐ。
    context.RestoreMainRenderTargets(false);
    if(!result) return result;
    context.SetGraphicsPipeline(state.compositePipeline);
    if(!context.SetGraphicsDescriptorTables(state.compositePipeline, state.srv))
        return std::unexpected(Error(ErrorCategory::Graphics, 150, "Selection mask binding failed."));
    context.Draw(3); context.RestoreMainRenderTargets(); return {};
}
void SelectionOutline::Shutdown() noexcept {
    if(!impl_) return;
    if(impl_->srv.IsValid()) static_cast<void>(impl_->resources->RetirePersistentShaderResource(impl_->srv));
    if(impl_->rtv.IsValid()) static_cast<void>(impl_->descriptors->RenderTargets().Free(impl_->rtv));
    impl_.reset();
}
}
