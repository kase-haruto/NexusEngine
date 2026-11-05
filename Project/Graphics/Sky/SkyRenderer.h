#pragma once

#include "Graphics/Environment/EnvironmentLightingData.h"
#include "Graphics/Environment/EnvironmentLighting.h"
#include "Graphics/Renderer/GraphicsRenderer.h"
#include "Graphics/Pipeline/GraphicsPipeline.h"
#include "Graphics/Resource/ConstantBuffer.h"
#include "Foundation/Math/Matrix4x4.h"

namespace NexusEngine {
    /** Infinite background, independent of camera translation and scene geometry. */
    class SkyRenderer final {
    public:
        [[nodiscard]] Result<void> Initialize(const GraphicsRendererInitializationContext&);
        [[nodiscard]] Result<void> Prepare(const RenderScene&);
        [[nodiscard]] Result<void> Render(GraphicsContext&, const Matrix4x4& view,
            const Matrix4x4& projection, const EnvironmentDrawData&, float intensity = 1);
        [[nodiscard]] Result<void> Render(GraphicsContext&, const RenderScene&);
        void Shutdown() noexcept;
    private:
        GraphicsPipeline pipeline_;
        ConstantBuffer constants_;
        EnvironmentLighting environment_;
    };
}
