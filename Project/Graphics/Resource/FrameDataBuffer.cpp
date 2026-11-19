#include "FrameDataBuffer.h"
#include "Graphics/Renderer/GraphicsResourceFactory.h"
#include <algorithm>

namespace NexusEngine {
struct FrameDataBuffer::Allocation {
    ConstantBuffer buffer;
    std::vector<ShaderResourceRef> views;
};
FrameDataBuffer::FrameDataBuffer() = default;
FrameDataBuffer::~FrameDataBuffer() {
    Shutdown();
}
Result<uint32_t> FrameDataBuffer::Write(GraphicsResourceFactory& factory, uint32_t frame, std::span<const uint8_t> bytes) {
    if(bytes.empty() || bytes.size() % 4 || bytes.size() > 16 * 1024 * 1024 || (factory_ && factory_ != &factory)) {
        return std::unexpected(Error(ErrorCategory::Graphics, 144, "Invalid frame data buffer payload or owner"));
    }
    if(allocations_.empty() || allocations_.back()->buffer.GetDataSize() < bytes.size()) {
        size_t capacity = allocations_.empty() ? 256 : allocations_.back()->buffer.GetDataSize();
        while(capacity < bytes.size()) {
            capacity *= 2;
        }
        auto allocation = std::make_unique<Allocation>();
        const auto created = factory.CreateConstantBuffer(allocation->buffer, capacity);
        if(!created) {
            return std::unexpected(created.error());
        }
        auto views = factory.CreatePersistentRawBufferShaderResources(allocation->buffer);
        if(!views) {
            return std::unexpected(views.error());
        }
        allocation->views = std::move(*views);
        allocations_.push_back(std::move(allocation));
        factory_ = &factory;
    }
    auto& allocation = *allocations_.back();
    const auto written = allocation.buffer.Write(frame, bytes);
    if(!written || frame >= allocation.views.size()) {
        return written ? std::unexpected(Error(ErrorCategory::Graphics, 144, "Invalid frame data buffer frame")) : std::unexpected(written.error());
    }
    return allocation.views[frame].index;
}
void FrameDataBuffer::Shutdown() noexcept {
    if(factory_) {
        for(auto& allocation : allocations_) {
            for(const auto view : allocation->views) {
                static_cast<void>(factory_->RetirePersistentShaderResource(view));
            }
        }
    }
    allocations_.clear();
    factory_ = nullptr;
}
}
