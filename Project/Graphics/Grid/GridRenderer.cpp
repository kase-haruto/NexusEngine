#include "GridRenderer.h"
#include "Graphics/Renderer/GraphicsContext.h"
#include "Graphics/Renderer/GraphicsResourceFactory.h"
namespace NexusEngine {
    Result<void> GridRenderer::Initialize(const GraphicsRendererInitializationContext& context) {
        Shader vertex, pixel;
        auto result = vertex.Initialize({context.shaderDirectory / L"Grid/Grid.hlsl", L"VSMain", L"vs_6_6"}, ShaderStage::Vertex);
        if(!result) return result;
        result = pixel.Initialize({context.shaderDirectory / L"Grid/Grid.hlsl", L"PSMain", L"ps_6_6"}, ShaderStage::Pixel);
        if(!result) return result;
        GraphicsPipelineDesc desc;
        desc.hdrRenderTarget = true; desc.doubleSided = true; desc.alphaBlending = true;
        desc.depthStencilFormat = DepthStencilFormat::D32Float;
        desc.depthStencil.depthTestEnabled = true; desc.depthStencil.depthWriteEnabled = false;
        desc.depthStencil.compareOperation = CompareOperation::LessEqual;
        result = context.resources.CreateGraphicsPipeline(pipeline_, std::move(vertex), std::move(pixel), desc);
        if(!result) return result;
        result = context.resources.CreateConstantBuffer(constants_, 176);
        if(!result) Shutdown();
        return result;
    }
    Result<void> GridRenderer::Render(GraphicsContext& context, const Matrix4x4& viewProjection,
        const std::array<float, 3>& camera, const GridSettings& settings) {
        if(!settings.enabled) return {};
        if(!settings.IsValid()) return std::unexpected(Error(ErrorCategory::Graphics, 96, "Invalid grid settings."));
        auto inverse = TryInverse(viewProjection);
        if(!inverse) return std::unexpected(Error(ErrorCategory::Graphics, 97, "Grid camera matrix is singular."));
        struct Data { Matrix4x4 viewProjection, inverseViewProjection; std::array<float, 4> cameraSpacing, plane, color; };
        static_assert(sizeof(Data) == 176);
        const Data data {viewProjection, *inverse, {camera[0], camera[1], camera[2], settings.spacing},
            {settings.height, settings.fadeDistance, 0, 0}, settings.color};
        auto result = constants_.Write(context.GetFrameIndex(), {reinterpret_cast<const uint8_t*>(&data), sizeof(data)});
        if(!result) return result;
        context.RestoreMainRenderTargets(); context.SetGraphicsPipeline(pipeline_);
        result = context.SetGraphicsConstantBufferTable(pipeline_, constants_);
        if(!result) return result;
        context.SetPrimitiveTopology(PrimitiveTopology::TriangleList); context.Draw(3);
        return {};
    }
    void GridRenderer::Shutdown() noexcept { constants_.Shutdown(); pipeline_.Shutdown(); }
}
