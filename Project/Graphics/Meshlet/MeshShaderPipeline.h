#pragma once
#include <filesystem>
#include <memory>
#include "Foundation/Error/Result.h"
#include "Foundation/Math/Matrix4x4.h"
#include "MeshletDebugSettings.h"
namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * MeshCullingView
 * - 既存 RenderCamera の ViewProjection と Object transform から準備する描画入力
 * - Frustum plane を local 空間へ引き戻し、非一様 scale / shear も保守的に判定する
 *---------------------------------------------------------------------------------------*/
struct MeshCullingView {
    Matrix4x4 localToClip; //< row-vector World * ViewProjection
};
/*-----------------------------------------------------------------------------------------
 * MeshShaderPipeline
 * - AS / MS / PS と専用 Root Signature / PSO を所有する独立 Pipeline
 * - Native API は DX12 Backend に限定し、従来 GraphicsPipeline を変更しない
 *---------------------------------------------------------------------------------------*/
class MeshShaderPipeline final {
public:
    MeshShaderPipeline() noexcept;
    ~MeshShaderPipeline() noexcept;
    MeshShaderPipeline(const MeshShaderPipeline&) = delete;
    MeshShaderPipeline& operator=(const MeshShaderPipeline&) = delete;
    /** \brief GPU idle 後に Pipeline を解放する */
    void Shutdown() noexcept;
    /** \brief 対応 GPU で Pipeline の作成に成功したか返す */
    [[nodiscard]] bool IsInitialized() const noexcept;
private:
    friend class GraphicsResourceFactory;
    friend class GraphicsContext;
    /** \brief 既存 SM6.6 / DXC 方針で AS / MS / PS を初期化する */
    [[nodiscard]] Result<void> Initialize(void* device, const std::filesystem::path& shaders);
    /** \brief Root CBV / SRV と Culling 定数を Bind し、Backend 内だけで DispatchMesh する */
    [[nodiscard]] bool Dispatch(void* commands, uint64_t constants, const uint64_t* buffers,
        const MeshCullingView& view, uint32_t first, uint32_t count, const MeshletDebugSettings& settings) const noexcept;
    class Impl;
    std::unique_ptr<Impl> impl_; //< PSO / RootSignature / Shader の唯一の所有者
};
}
