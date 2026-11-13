#include "SkyRenderer.h"
#include "Graphics/Renderer/GraphicsContext.h"
#include "Graphics/Renderer/GraphicsResourceFactory.h"
#include "Graphics/Renderer/RenderScene.h"

namespace NexusEngine {
    Result<void> SkyRenderer::Initialize(const GraphicsRendererInitializationContext& context) {
        Shader vertex, pixel;
        auto result = vertex.Initialize({context.shaderDirectory / L"Sky/Sky.hlsl", L"VSMain", L"vs_6_6"}, ShaderStage::Vertex);
        if(!result) return result;
        result = pixel.Initialize({context.shaderDirectory / L"Sky/Sky.hlsl", L"PSMain", L"ps_6_6"}, ShaderStage::Pixel);
        if(!result) return result;
        GraphicsPipelineDesc desc;
        desc.hdrRenderTarget = true;
        desc.doubleSided = true;
        desc.depthStencilFormat = DepthStencilFormat::D32Float;
        desc.depthStencil.depthTestEnabled = true;
        desc.depthStencil.depthWriteEnabled = false;
        desc.depthStencil.compareOperation = CompareOperation::LessEqual;
        result = context.resources.CreateGraphicsPipeline(pipeline_, std::move(vertex), std::move(pixel), desc);
        if(!result) return result;
        result = context.resources.CreateConstantBuffer(constants_, 176);
        if(!result) { Shutdown(); return result; }
        result = environment_.Initialize(context.resources, context.shaderDirectory);
        if(!result) Shutdown();
        return result;
    }

    Result<void> SkyRenderer::Render(GraphicsContext& context, const Matrix4x4& view,
        const Matrix4x4& projection, const EnvironmentDrawData& environment, float intensity) {
        // Remove translation before inversion, avoiding precision loss far from the origin.
        auto rotation = view;
        for(size_t i = 0; i < 3; ++i) rotation.At(3, i) = 0;
        const auto inverse = TryInverse(Multiply(rotation, projection));
        if(!inverse) return std::unexpected(Error(ErrorCategory::Graphics, 98, "Sky camera matrix is singular."));
        struct Data { Matrix4x4 inverseViewProjection; EnvironmentDrawData environment; std::array<float, 4> parameters; };
        static_assert(sizeof(Data) == 176);
        const Data data {*inverse, environment, {intensity, 0, 0, 0}};
        auto result = constants_.Write(context.GetFrameIndex(), {reinterpret_cast<const uint8_t*>(&data), sizeof(data)});
        if(!result) return result;
        context.RestoreMainRenderTargets();
        context.SetGraphicsPipeline(pipeline_);
        result = context.SetGraphicsConstantBufferTable(pipeline_, constants_);
        if(!result) return result;
        context.SetPrimitiveTopology(PrimitiveTopology::TriangleList);
        context.Draw(3);
        return {};
    }

    Result<void> SkyRenderer::Prepare(const RenderScene& scene) {
        if(!scene.skyIntensity || !scene.camera) return {};
        if(scene.atmosphere.enabled) return {};
        return environment_.Prepare(scene.environment);
    }

    Result<void> SkyRenderer::Render(GraphicsContext& context, const RenderScene& scene) {
        if(!scene.skyIntensity || !scene.camera) return {};
        auto settings = scene.environment;
        if(scene.atmosphere.enabled) { settings.atmosphere = scene.atmosphere; settings.enabled = true; settings.intensity = 1; }
        return Render(context, scene.camera->viewMatrix, scene.camera->projectionMatrix,
            environment_.GetDrawData(settings), *scene.skyIntensity);
    }

    void SkyRenderer::Shutdown() noexcept { environment_.Shutdown(); constants_.Shutdown(); pipeline_.Shutdown(); }
}
