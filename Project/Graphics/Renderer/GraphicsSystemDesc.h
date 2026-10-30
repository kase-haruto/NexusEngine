#pragma once

// c++
#include <array>
#include <cstdint>
#include <filesystem>
#include "Graphics/PostProcess/PostProcessSettings.h"

namespace NexusEngine {

#if defined(_DEBUG) || defined(NEXUS_DEVELOP)
	inline constexpr bool kDefaultGraphicsDiagnosticsEnabled = true;
#else
	inline constexpr bool kDefaultGraphicsDiagnosticsEnabled = false;
#endif

	/*-----------------------------------------------------------------------------------------
	 * WindowSurfaceDesc
	 * - Graphicsへ渡すネイティブWindowと描画領域の最小情報を保持する
	 * - ProjectWindowやDirectX固有型には依存しない
	 *---------------------------------------------------------------------------------------*/
	struct WindowSurfaceDesc {
		void* nativeHandle = nullptr;
		uint32_t width = 0;
		uint32_t height = 0;
	};

	/*-----------------------------------------------------------------------------------------
	 * GraphicsSystemDesc
	 * - Graphicsサブシステムの初期設定を保持する
	 *---------------------------------------------------------------------------------------*/
	struct GraphicsSystemDesc {
		PostProcessSettings postProcess;
		bool enableDebugLayer = kDefaultGraphicsDiagnosticsEnabled;
		bool enableGpuBasedValidation = false;
		bool enableDred = kDefaultGraphicsDiagnosticsEnabled;
		bool useWarpAdapter = false;
		bool enableVSync = true;
		std::array<float, 4> clearColor = { 0.08f, 0.12f, 0.20f, 1.0f }; // Linear Rec.709 RGB; alpha is linear.
		std::filesystem::path shaderDirectory = L"Shaders";
		std::filesystem::path assetDirectory = L"Resources/Assets";
		uint32_t bindlessResourceCapacity = 4096; //< CBV/SRV/UAV永続Bindless slot数
		uint32_t bindlessSamplerCapacity = 128;   //< Sampler永続Bindless slot数
		uint32_t transientResourceDescriptorsPerFrame = 256; //< Frameごとの初期予約数。不足時は同一Heapからこの単位で追加する
		uint32_t rtvDescriptorCapacity = 256; //< BackBufferとRenderTarget用RTV数
		uint32_t dsvDescriptorCapacity = 128; //< Depth/Shadow用DSV数
	};

} // namespace NexusEngine
