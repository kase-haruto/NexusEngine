#pragma once
#include "ConstantBuffer.h"
#include "Graphics/Descriptor/ShaderResourceRef.h"
#include <vector>

namespace NexusEngine {
class GraphicsResourceFactory;
/** \brief Frame slice別の可変長SRV。拡張時も旧allocationをShutdownまで保持しGPU参照を壊さない。 */
class FrameDataBuffer final {
public:
    FrameDataBuffer();
    ~FrameDataBuffer();
    Result<uint32_t> Write(GraphicsResourceFactory& factory, uint32_t frame, std::span<const uint8_t> bytes);
    /** \brief GPU idle後、factoryが生存している間に呼ぶ。 */
    void Shutdown() noexcept;
private:
    struct Allocation;
    std::vector<std::unique_ptr<Allocation>> allocations_;
    GraphicsResourceFactory* factory_ = nullptr;
};
}
