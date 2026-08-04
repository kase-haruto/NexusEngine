#pragma once

// c++
#include <array>
#include <cstdint>
#include <memory>

// engine
#include "Foundation/Error/Result.h"
#include "GraphicsSystemDesc.h"

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * GraphicsSystem
	 * - Graphicsサブシステムの初期化、1フレーム描画、Present、終了順序を組み立てる
	 * - DirectX 12型は実装内部へ隠し、SceneやResourceの管理は担当しない
	 *---------------------------------------------------------------------------------------*/
	class GraphicsSystem final {
	public:
		GraphicsSystem() noexcept;
		~GraphicsSystem() noexcept;
		GraphicsSystem(const GraphicsSystem&) = delete;
		GraphicsSystem& operator=(const GraphicsSystem&) = delete;

		/**
		 * \brief Window Surfaceへ描画するGraphicsサブシステムを初期化する
		 * \param surface Native Window Handleと初期描画サイズ
		 * \param desc Device診断、表示、Clear設定
		 * \return 初期化結果
		 */
		[[nodiscard]] Result<void> Initialize(const WindowSurfaceDesc& surface, const GraphicsSystemDesc& desc);
		/**
		 * \brief GPU完了後にGraphicsリソースを依存の逆順で破棄する
		 */
		void Shutdown() noexcept;
		/**
		 * \brief 現在のBackBufferを設定色でClearしてPresentする
		 * \return フレーム描画結果
		 */
		[[nodiscard]] Result<void> RenderFrame();
		/**
		 * \brief SwapChain BackBufferを新しいWindowサイズへ変更する
		 * \param width 新しい描画幅
		 * \param height 新しい描画高
		 * \return リサイズ結果
		 */
		[[nodiscard]] Result<void> Resize(uint32_t width, uint32_t height);
		/**
		 * \brief 後続フレームで使用するClear Colorを設定する
		 * \param color RGBA順のClear Color
		 */
		void SetClearColor(const std::array<float, 4>& color) noexcept;

	private:
		class Impl;
		std::unique_ptr<Impl> impl_; //< DirectX 12具体実装の唯一の所有者
	};

} // namespace NexusEngine
