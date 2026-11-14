#pragma once
#include "Graphics/Renderer/GraphicsRenderer.h"
#include "Graphics/Pipeline/GraphicsPipeline.h"
#include "Graphics/Resource/ConstantBuffer.h"
#include "Foundation/Math/Matrix4x4.h"
#include "Foundation/Math/DebugDrawLine.h"
#include <span>

namespace NexusEngine {
    /** 値snapshotの診断線をHDR Sceneへ描く。Frame別CBでGPU実行中のデータを保護する。 */
    class DebugDrawRenderer final {
    public:
        [[nodiscard]] Result<void> Initialize(const GraphicsRendererInitializationContext& context);
        [[nodiscard]] Result<void> Render(GraphicsContext& context, const Matrix4x4& viewProjection, std::span<const DebugDrawLine> lines);
        void Shutdown() noexcept;
    private:
        GraphicsPipeline depth_, overlay_;
        ConstantBuffer depthConstants_, overlayConstants_;
    };
}
