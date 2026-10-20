#pragma once
#include <memory>
#include "MeshletData.h"
#include "Foundation/Error/Result.h"
namespace NexusEngine {
class CommandQueue;
/*-----------------------------------------------------------------------------------------
 * MeshletResource
 * - Upload 済み Meshlet / remap / packed primitive / bounds Buffer の唯一の所有者
 * - 元頂点 Buffer は MeshResource 所有。Geometry 世代で整合を検証する
 * - Root SRV を使用し、Meshlet ごとの Descriptor は作らない
 *---------------------------------------------------------------------------------------*/
class MeshletResource final {
public:
    MeshletResource() noexcept;
    ~MeshletResource() noexcept;
    MeshletResource(const MeshletResource&) = delete;
    MeshletResource& operator=(const MeshletResource&) = delete;
    /** \brief GPU idle 後に解放する。Factory / GraphicsSystem より先に破棄する */
    void Shutdown() noexcept;
    /** \brief CPU Submesh と同じ順序の Meshlet range を返す */
    [[nodiscard]] const std::vector<MeshletRange>& GetRanges() const noexcept;
    /** \brief 元 Mesh の再生成による不一致を検出する */
    [[nodiscard]] uint64_t GetGeometryGeneration() const noexcept;
private:
    friend class GraphicsResourceFactory;
    friend class GraphicsContext;
    /** \brief 初期化時に同期 Upload。完了まで staging を保持する */
    [[nodiscard]] Result<void> Initialize(void* device, CommandQueue* queue, const MeshletData& data, uint64_t generation);
    /** \brief Backend 内でだけ解釈する Buffer address */
    [[nodiscard]] uint64_t GetGpuAddress(uint32_t buffer) const noexcept;
    class Impl;
    std::unique_ptr<Impl> impl_; //< Native Buffer を隠す唯一の所有者
};
}
