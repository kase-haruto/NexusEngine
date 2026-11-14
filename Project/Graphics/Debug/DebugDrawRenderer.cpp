#include "DebugDrawRenderer.h"
#include "Graphics/Renderer/GraphicsContext.h"
#include "Graphics/Renderer/GraphicsResourceFactory.h"

namespace NexusEngine {
    namespace {
        struct GpuLine {
            std::array<float, 4> start, end, color;
        };
        struct DebugConstants {
            Matrix4x4 viewProjection;
            std::array<GpuLine, 1024> lines;
        };
        static_assert(sizeof(DebugConstants) == 49216);
    }
    Result<void> DebugDrawRenderer::Initialize(const GraphicsRendererInitializationContext& context) {
        for(int pass = 0; pass < 2; ++pass) {
            Shader vertex, pixel;
            auto result = vertex.Initialize({context.shaderDirectory / L"Debug/DebugLines.hlsl", L"VSMain", L"vs_6_6"}, ShaderStage::Vertex);
            if(!result) {
                Shutdown();
                return result;
            }
            result = pixel.Initialize({context.shaderDirectory / L"Debug/DebugLines.hlsl", L"PSMain", L"ps_6_6"}, ShaderStage::Pixel);
            if(!result) {
                Shutdown();
                return result;
            }
            GraphicsPipelineDesc desc;
            desc.lineTopology = true;
            desc.hdrRenderTarget = true;
            desc.alphaBlending = true;
            desc.depthStencilFormat = DepthStencilFormat::D32Float;
            desc.depthStencil.depthTestEnabled = pass == 0;
            desc.depthStencil.depthWriteEnabled = false;
            desc.depthStencil.compareOperation = CompareOperation::LessEqual;
            result = context.resources.CreateGraphicsPipeline(pass == 0 ? depth_ : overlay_, std::move(vertex), std::move(pixel), desc);
            if(!result) {
                Shutdown();
                return result;
            }
        }
        auto result = context.resources.CreateConstantBuffer(depthConstants_, sizeof(DebugConstants));
        if(result) {
            result = context.resources.CreateConstantBuffer(overlayConstants_, sizeof(DebugConstants));
        }
        if(!result) {
            Shutdown();
        }
        return result;
    }
    Result<void> DebugDrawRenderer::Render(GraphicsContext& context, const Matrix4x4& viewProjection, std::span<const DebugDrawLine> lines) {
        if(lines.empty()) {
            return {};
        }
        if(lines.size() > 1024) {
            return std::unexpected(Error(ErrorCategory::Graphics, 98, "Debug draw limit exceeded."));
        }
        DebugConstants data{};
        data.viewProjection = viewProjection;
        context.RestoreMainRenderTargets();
        context.SetPrimitiveTopology(PrimitiveTopology::LineList);
        // 各passに独立したFrame別CBを使い、記録済みDrawの入力を後続passで上書きしない。
        for(int pass = 0; pass < 2; ++pass) {
            size_t count = 0;
            for(const auto& line : lines) {
                if(line.depthTest == (pass == 0)) {
                    data.lines[count++] = {{line.start[0], line.start[1], line.start[2], 1},
                        {line.end[0], line.end[1], line.end[2], 1}, line.color};
                }
            }
            if(count == 0) {
                continue;
            }
            auto& constants = pass == 0 ? depthConstants_ : overlayConstants_;
            auto result = constants.Write(context.GetFrameIndex(), {reinterpret_cast<const uint8_t*>(&data), sizeof(data)});
            if(!result) {
                return result;
            }
            auto& pipeline = pass == 0 ? depth_ : overlay_;
            context.SetGraphicsPipeline(pipeline);
            result = context.SetGraphicsConstantBufferTable(pipeline, constants);
            if(!result) {
                return result;
            }
            context.Draw(static_cast<uint32_t>(count * 2));
        }
        return {};
    }
    void DebugDrawRenderer::Shutdown() noexcept {
        overlayConstants_.Shutdown();
        depthConstants_.Shutdown();
        overlay_.Shutdown();
        depth_.Shutdown();
    }
}
