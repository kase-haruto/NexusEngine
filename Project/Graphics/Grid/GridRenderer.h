#pragma once
#include "GridSettings.h"
#include "Graphics/Pipeline/GraphicsPipeline.h"
#include "Graphics/Resource/ConstantBuffer.h"
#include "Graphics/Renderer/GraphicsRenderer.h"
#include "Foundation/Math/Matrix4x4.h"
namespace NexusEngine {
    // Shader-backed scene pass using only the public graphics API.
    class GridRenderer final {
    public:
        [[nodiscard]] Result<void> Initialize(const GraphicsRendererInitializationContext&);
        [[nodiscard]] Result<void> Render(GraphicsContext&, const Matrix4x4& viewProjection,
            const std::array<float, 3>& cameraPosition, const GridSettings&);
        void Shutdown() noexcept;
    private:
        GraphicsPipeline pipeline_;
        ConstantBuffer constants_;
    };
}
