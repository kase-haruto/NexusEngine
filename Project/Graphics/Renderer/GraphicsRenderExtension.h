#pragma once

#include <cstdint>
#include "Foundation/Error/Result.h"
#include "Graphics/Resource/TextureResource.h"
#include "Graphics/Resource/Depthstencil/DepthstencilTypes.h"

namespace NexusEngine {
	class DescriptorAllocator;
	class GraphicsContext;

	struct GraphicsRenderExtensionContext {
		void* nativeDevice = nullptr; //< Graphics backendが所有するDeviceへの非所有参照
		void* nativeCommandQueue = nullptr; //< Graphics backendが所有するQueueへの非所有参照
		DescriptorAllocator* resourceDescriptors = nullptr; //< 共有CBV/SRV/UAV allocator
		void* nativeWindow = nullptr; //< Platform Windowの非所有Handle
		uint32_t framesInFlight = 0; //< FrameContext数
		TextureFormat renderTargetFormat = TextureFormat::Rgba8Unorm; //< Main BackBuffer形式
		DepthStencilFormat depthStencilFormat = DepthStencilFormat::None; //< Main Depth Target形式
	};

	/*-----------------------------------------------------------------------------------------
	 * IGraphicsRenderExtension
	 * - GraphicsSystemへ任意の上位描画処理を接続するための依存逆転境界
	 * - Graphics低層は具象Editor UIやDear ImGuiへ依存しない
	 *---------------------------------------------------------------------------------------*/
	class IGraphicsRenderExtension {
	public:
		virtual ~IGraphicsRenderExtension() = default;
		[[nodiscard]] virtual Result<void> Initialize(const GraphicsRenderExtensionContext& context) = 0;
		virtual void BeginFrame() = 0;
		virtual void Record(GraphicsContext& context) = 0;
		virtual void Shutdown() noexcept = 0;
	};
} // namespace NexusEngine
