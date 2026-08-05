#pragma once

#include <cstdint>
#include <d3d12.h>
#include <dxgiformat.h>

#include "Foundation/Error/Result.h"

namespace NexusEngine {
	class DescriptorAllocator;

	struct GraphicsRenderExtensionContext {
		ID3D12Device* device = nullptr; //< GraphicsDevice所有の非所有参照
		ID3D12CommandQueue* commandQueue = nullptr; //< CommandQueue所有の非所有参照
		DescriptorAllocator* resourceDescriptors = nullptr; //< 共有CBV/SRV/UAV allocator
		void* nativeWindow = nullptr; //< Platform Windowの非所有Handle
		uint32_t framesInFlight = 0; //< FrameContext数
		DXGI_FORMAT renderTargetFormat = DXGI_FORMAT_UNKNOWN; //< Main BackBuffer形式
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
		virtual void Record(ID3D12GraphicsCommandList* commandList) = 0;
		virtual void Shutdown() noexcept = 0;
	};
} // namespace NexusEngine
