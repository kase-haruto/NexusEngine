#pragma once

namespace NexusEngine {

#if defined(_DEBUG) || defined(NEXUS_DEVELOP)
	inline constexpr bool kDefaultGraphicsDebugEnabled = true;
#else
	inline constexpr bool kDefaultGraphicsDebugEnabled = false;
#endif

	/*-----------------------------------------------------------------------------------------
	 * GraphicsDeviceDesc
	 * - GraphicsDevice生成時のAdapterおよび診断機能の設定を保持する
	 * - Command QueueやSwapChainの設定は管理しない
	 *---------------------------------------------------------------------------------------*/
	struct GraphicsDeviceDesc {
		bool enableDebugLayer			= kDefaultGraphicsDebugEnabled;
		bool enableGpuBasedValidation	= false;
		bool enableDred					= kDefaultGraphicsDebugEnabled;
		bool useWarpAdapter				= false;
	};

} // namespace NexusEngine
