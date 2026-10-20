#include "Graphics/Meshlet/MeshShaderPipeline.h"
#include "Graphics/Meshlet/MeshletConfig.h"
#include "Graphics/Shader/Shader.h"
#include "Graphics/Material/PrimitiveDrawData.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <climits>
#include <cstddef>
namespace NexusEngine {
namespace {
// Root index は Backend のこの定義へ集約。b0, b1, t0..t4 は HLSL と対になる。
enum Root : uint32_t { Draw = 0, Culling = 1, Vertices = 2, Meshlets = 3, Remap = 4, Primitives = 5, Bounds = 6, RootCount = 7 };
template<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Kind, class T>
struct Subobject {
    struct Data {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = Kind; //< stream subobject の識別子
        T value {}; //< 識別子に対応した Native descriptor
    };
    // PSO stream は subobject 全体を pointer 境界へ揃える契約。
    // pointer を持つ union の自然 alignment により末尾 padding を確保し、
    // alignas の意図的 padding に対する MSVC C4324 を発生させない。
    union {
        Data data; //< 使用する active member
        void* alignment; //< alignment のみを規定する未使用 member
    };
    Subobject() noexcept : data {} {}
};
static_assert(alignof(Subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND, D3D12_BLEND_DESC>) == alignof(void*));
static_assert(sizeof(Subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND, D3D12_BLEND_DESC>) % sizeof(void*) == 0);
struct Stream {
    Subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, ID3D12RootSignature*> root;
    Subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS, D3D12_SHADER_BYTECODE> as;
    Subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, D3D12_SHADER_BYTECODE> ms;
    Subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, D3D12_SHADER_BYTECODE> ps;
    Subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND, D3D12_BLEND_DESC> blend;
    Subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER, D3D12_RASTERIZER_DESC> raster;
    Subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL, D3D12_DEPTH_STENCIL_DESC> depth;
    Subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY, D3D12_PRIMITIVE_TOPOLOGY_TYPE> topology;
    Subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS, D3D12_RT_FORMAT_ARRAY> rtv;
    Subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT, DXGI_FORMAT> dsv;
    Subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC, DXGI_SAMPLE_DESC> samples;
    Subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK, UINT> mask;
};
struct CullingConstants {
    std::array<std::array<float, 4>, 6> planes {}; //< 正規化済み local-space Frustum
    uint32_t first = 0, count = 0, mode = 0, enabled = 0; //< b1 最後の16bytes
};
static_assert(sizeof(CullingConstants) == 112);
static_assert(offsetof(CullingConstants, first) == 96 && offsetof(CullingConstants, count) == 100 &&
    offsetof(CullingConstants, mode) == 104 && offsetof(CullingConstants, enabled) == 108);
constexpr uint32_t kCullingDwords = sizeof(CullingConstants) / sizeof(uint32_t);
}
class MeshShaderPipeline::Impl {
public:
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root; //< 専用 Root Signature
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso; //< AS/MS/PS PSO
    std::array<Shader, 3> shaders; //< 初期化時 compile / reflection
};
MeshShaderPipeline::MeshShaderPipeline() noexcept = default;
MeshShaderPipeline::~MeshShaderPipeline() noexcept = default;
void MeshShaderPipeline::Shutdown() noexcept { impl_.reset(); }
bool MeshShaderPipeline::IsInitialized() const noexcept { return bool(impl_); }
Result<void> MeshShaderPipeline::Initialize(void* native, const std::filesystem::path& directory) {
    if(!native || impl_) return std::unexpected(Error(ErrorCategory::Graphics, 82, "Invalid mesh pipeline state."));
    auto* device = static_cast<ID3D12Device*>(native);
    Microsoft::WRL::ComPtr<ID3D12Device2> device2;
    HRESULT hr = device->QueryInterface(IID_PPV_ARGS(&device2));
    if(FAILED(hr)) return std::unexpected(MakeDirectXError(82, hr, "Mesh pipeline device interface unavailable."));
    auto next = std::make_unique<Impl>();
    const std::array files {L"Mesh/MeshletAS.hlsl", L"Mesh/MeshletMS.hlsl", L"Mesh/MeshletPS.hlsl"};
    const std::array profiles {L"as_6_6", L"ms_6_6", L"ps_6_6"};
    const std::array stages {ShaderStage::Amplification, ShaderStage::Mesh, ShaderStage::Pixel};
    for(size_t i = 0; i < 3; ++i) {
        auto status = next->shaders[i].Initialize({directory / files[i], L"main", profiles[i]}, stages[i]);
        if(!status) return status;
        // Reflection で固定 Root SRV/CBV 契約外の Binding を起動時に拒否する。
        for(const auto& b : next->shaders[i].GetMetadata().resources) {
            const bool cb = b.type == ShaderResourceType::ConstantBuffer && b.bindPoint <= 1 &&
                b.constantBufferSize == (b.bindPoint == 0 ? sizeof(PrimitiveDrawData) : 112U);
            const bool srv = b.type == ShaderResourceType::StructuredBuffer && b.bindPoint <= 4;
            if(b.registerSpace != 0 || b.bindCount != 1 || (!cb && !srv))
                return std::unexpected(Error(ErrorCategory::Graphics, 82, "Mesh shader reflection/root binding mismatch."));
        }
    }
    ////////////////////////////////////////////////////////////////////////////
    // Mesh Pipeline は IA を使用しない。Geometry は Root SRV、Material は既存 Bindless Heap。
    ////////////////////////////////////////////////////////////////////////////
    std::array<D3D12_ROOT_PARAMETER1, RootCount> parameters {};
    parameters[Draw].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[Draw].Descriptor = {0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE};
    parameters[Culling].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[Culling].Constants = {1, 0, kCullingDwords};
    for(uint32_t i = Vertices; i <= Bounds; ++i) {
        parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        parameters[i].Descriptor = {i - Vertices, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC};
    }
    for(auto& p : parameters) p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootDesc {};
    rootDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rootDesc.Desc_1_1.NumParameters = RootCount; rootDesc.Desc_1_1.pParameters = parameters.data();
    rootDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
                             D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;
    Microsoft::WRL::ComPtr<ID3DBlob> serialized, errors;
    hr = D3D12SerializeVersionedRootSignature(&rootDesc, &serialized, &errors);
    if(FAILED(hr)) return std::unexpected(MakeDirectXError(82, hr, "Mesh root serialization failed."));
    hr = device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&next->root));
    if(FAILED(hr)) return std::unexpected(MakeDirectXError(82, hr, "Mesh root creation failed."));
    Stream stream;
    stream.root.data.value = next->root.Get();
    const auto as = next->shaders[0].GetBytecode(), ms = next->shaders[1].GetBytecode(), ps = next->shaders[2].GetBytecode();
    stream.as.data.value = {as.data(), as.size()}; stream.ms.data.value = {ms.data(), ms.size()}; stream.ps.data.value = {ps.data(), ps.size()};
    for(auto& target : stream.blend.data.value.RenderTarget)
        target = {FALSE, FALSE, D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
                  D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD, D3D12_LOGIC_OP_NOOP, D3D12_COLOR_WRITE_ENABLE_ALL};
    stream.raster.data.value = {D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_BACK, FALSE, D3D12_DEFAULT_DEPTH_BIAS,
        D3D12_DEFAULT_DEPTH_BIAS_CLAMP, D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS, TRUE, FALSE, FALSE, 0, D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF};
    stream.depth.data.value.DepthEnable = TRUE; stream.depth.data.value.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    stream.depth.data.value.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    stream.depth.data.value.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    stream.depth.data.value.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    stream.depth.data.value.FrontFace = stream.depth.data.value.BackFace = {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS};
    stream.topology.data.value = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    stream.rtv.data.value.NumRenderTargets = 1; stream.rtv.data.value.RTFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    stream.dsv.data.value = DXGI_FORMAT_D32_FLOAT; stream.samples.data.value = {1, 0}; stream.mask.data.value = UINT_MAX;
    const D3D12_PIPELINE_STATE_STREAM_DESC desc {sizeof(stream), &stream};
    hr = device2->CreatePipelineState(&desc, IID_PPV_ARGS(&next->pso));
    if(FAILED(hr)) return std::unexpected(MakeDirectXError(82, hr, "Mesh PSO creation failed."));
    impl_ = std::move(next);
    return {};
}
bool MeshShaderPipeline::Dispatch(void* native, uint64_t cb, const uint64_t* buffers,
    const MeshCullingView& view, uint32_t first, uint32_t count, const MeshletDebugSettings& settings) const noexcept {
    if(!impl_ || !native || !cb || !buffers || !count || uint64_t(first) + count > UINT32_MAX) return false;
    for(size_t i = 0; i < 5; ++i) if(!buffers[i]) return false;
    // GraphicsContext が取得・保持した interface。QueryInterface は Frame ごとに最大1回。
    auto* commands = static_cast<ID3D12GraphicsCommandList6*>(native);
    CullingConstants constants;
    constants.count = count; constants.mode = static_cast<uint32_t>(settings.visualization); constants.enabled = settings.enableCulling ? 1U : 0U;
    ////////////////////////////////////////////////////////////////////////////
    // D3D clip 条件 -w<=x,y<=w / 0<=z<=w の6平面を local 空間へ引き戻す。
    // Sphere を world 空間へ近似変換しないので shear / 非一様 scale でも誤 Cull しない。
    ////////////////////////////////////////////////////////////////////////////
    const auto& m = view.localToClip;
    for(size_t row = 0; row < 4; ++row) {
        constants.planes[0][row] = m.At(row, 3) + m.At(row, 0);
        constants.planes[1][row] = m.At(row, 3) - m.At(row, 0);
        constants.planes[2][row] = m.At(row, 3) + m.At(row, 1);
        constants.planes[3][row] = m.At(row, 3) - m.At(row, 1);
        constants.planes[4][row] = m.At(row, 2);
        constants.planes[5][row] = m.At(row, 3) - m.At(row, 2);
    }
    for(auto& plane : constants.planes) {
        const double length = std::sqrt(double(plane[0])*plane[0] + double(plane[1])*plane[1] + double(plane[2])*plane[2]);
        bool valid = std::isfinite(length) && length > 1e-20;
        for(float component : plane) valid &= std::isfinite(component);
        if(!valid) plane = {}; // 不正平面は保守的に全可視として扱う。
        else {
            for(float& component : plane) component = float(double(component) / length);
            for(float component : plane) valid &= std::isfinite(component);
            if(!valid) plane = {};
        }
    }
    commands->SetGraphicsRootSignature(impl_->root.Get()); commands->SetPipelineState(impl_->pso.Get());
    commands->SetGraphicsRootConstantBufferView(Draw, cb);
    for(uint32_t i = 0; i < 5; ++i) commands->SetGraphicsRootShaderResourceView(Vertices + i, buffers[i]);
    ////////////////////////////////////////////////////////////////////////////
    // 1 AS group が32 Meshlet を処理。巨大 Mesh は API の X group 上限で分割する。
    // 定数は Command 内にコピーされ、同一 CB を複数 draw で上書きしない。
    ////////////////////////////////////////////////////////////////////////////
    constexpr uint32_t kBatch = 65535U * NEXUS_MESHLET_AS_THREADS;
    for(uint32_t offset = 0; offset < count;) {
        const uint32_t batch = (count - offset < kBatch) ? count - offset : kBatch;
        constants.first = first + offset; constants.count = batch;
        commands->SetGraphicsRoot32BitConstants(Culling, kCullingDwords, &constants, 0);
        commands->DispatchMesh((batch + NEXUS_MESHLET_AS_THREADS - 1) / NEXUS_MESHLET_AS_THREADS, 1, 1);
        offset += batch;
    }
    return true;
}
}
