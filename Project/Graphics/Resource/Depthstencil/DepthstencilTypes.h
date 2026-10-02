#pragma once

// c++
#include <cstdint>

namespace NexusEngine {

	/** Graphics APIに依存しないDepth/Stencil Resource format。 */
	enum class DepthStencilFormat : uint8_t {
		None,
		D32Float
	};

	/** Graphics APIに依存しないDepth比較方法。 */
	enum class CompareOperation : uint8_t {
		Never,
		Less,
		Equal,
		LessEqual,
		Greater,
		NotEqual,
		GreaterEqual,
		Always
	};

	/*-----------------------------------------------------------------------------------------
	 * DepthStencilBufferDesc
	 * - Depth BufferのサイズとFormatだけを表すBackend非依存の記述
	 *---------------------------------------------------------------------------------------*/
	struct DepthStencilBufferDesc {
		uint32_t width = 0;
		uint32_t height = 0;
		DepthStencilFormat format = DepthStencilFormat::D32Float;
	};

	/** Graphics Pipelineが使用するDepth Test / Write設定。 */
	struct DepthStencilStateDesc {
		bool depthTestEnabled = false;
		bool depthWriteEnabled = false;
		CompareOperation compareOperation = CompareOperation::Less;
	};

} // namespace NexusEngine
