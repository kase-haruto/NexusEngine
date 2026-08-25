#pragma once

// c++
#include <filesystem>

// engine
#include "Foundation/Error/Result.h"

namespace NexusEngine {
	class GraphicsContext;
	class GraphicsResourceFactory;

	/*-----------------------------------------------------------------------------------------
	 * GraphicsRendererInitializationContext
	 * - Renderer初期化に必要なBackend非依存サービスと設定だけを束ねる
	 * - 参照先はGraphicsSystemが所有し、RendererのShutdown完了まで有効
	 *---------------------------------------------------------------------------------------*/
	struct GraphicsRendererInitializationContext {
		GraphicsResourceFactory& resources;
		const std::filesystem::path& shaderDirectory;
	};

	/*-----------------------------------------------------------------------------------------
	 * IGraphicsRenderer
	 * - RenderSceneなど上位データをGPU描画命令へ変換するRendererのライフサイクル契約
	 * - SwapChain、Frame Fence、Native Graphics API、Editor UIは担当しない
	 *---------------------------------------------------------------------------------------*/
	class IGraphicsRenderer {
	public:
		virtual ~IGraphicsRenderer() = default;
		[[nodiscard]] virtual Result<void> Initialize(const GraphicsRendererInitializationContext& context) = 0;
		virtual void Render(GraphicsContext& context) = 0;
		virtual void Shutdown() noexcept = 0;
	};
} // namespace NexusEngine
