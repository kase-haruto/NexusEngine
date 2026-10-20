#pragma once

#include <cstdint>

// engine
#include "Foundation/Error/Result.h"

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * IFrameworkUpdateClient
	 * - Frameworkのframe loopからApplication/Level更新を受ける境界
	 * - 更新対象の所有、描画、Window event処理は担当しない
	 *---------------------------------------------------------------------------------------*/
	class IFrameworkUpdateClient {
	public:
		virtual ~IFrameworkUpdateClient() = default;
		[[nodiscard]] virtual Result<void> Update(float deltaTime) = 0;
		/** \brief Update完了後にViewport寸法を使って描画入力を抽出する */
		[[nodiscard]] virtual Result<void> PrepareRender(uint32_t width, uint32_t height) {
			static_cast<void>(width);
			static_cast<void>(height);
			return {};
		}
	};

} // namespace NexusEngine
