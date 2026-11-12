#pragma once

#include <cstdint>

// engine
#include "Foundation/Error/Result.h"
#include "Foundation/Input/InputFrame.h"

namespace NexusEngine {
    class GraphicsSystem;
    class IGraphicsRenderer;

	/*-----------------------------------------------------------------------------------------
	 * IFrameworkUpdateClient
	 * - Frameworkのframe loopからApplication/Level更新を受ける境界
	 * - 更新対象の所有、描画、Window event処理は担当しない
	 *---------------------------------------------------------------------------------------*/
	class IFrameworkUpdateClient {
	public:
		virtual ~IFrameworkUpdateClient() = default;
        /** \brief Window eventの値コピーをUpdate直前に受け取る。Window自体は所有しない */
        virtual void SetFrameInput(const InputFrame& input) { static_cast<void>(input); }
        /** \brief Renderer初期化後だけGraphicsを借用する。動的asset公開の同期点に使用する */
        virtual Result<void> AttachGraphics(GraphicsSystem& graphics,IGraphicsRenderer* renderer) {
            static_cast<void>(graphics); static_cast<void>(renderer); return {};
        }
        /** \brief Graphics破棄前に借用参照を解除する。GPU/Windowの所有権は移さない */
        virtual void DetachGraphics() noexcept {}
		[[nodiscard]] virtual Result<void> Update(float deltaTime) = 0;
		/** \brief Update完了後にViewport寸法を使って描画入力を抽出する */
		[[nodiscard]] virtual Result<void> PrepareRender(uint32_t width, uint32_t height) {
			static_cast<void>(width);
			static_cast<void>(height);
			return {};
		}
	};

} // namespace NexusEngine
