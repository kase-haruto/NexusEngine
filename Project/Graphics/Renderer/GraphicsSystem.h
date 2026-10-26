#pragma once

// c++
#include <array>
#include <cstdint>
#include <memory>

// engine
#include "Foundation/Error/Result.h"
#include "GraphicsSystemDesc.h"
#include "Graphics/Device/GraphicsCapabilities.h"
#include "Graphics/Viewport/ViewportImage.h"

namespace NexusEngine {
	class IGraphicsRenderer;
	class IGraphicsRenderExtension;

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
		/** ライブコーディング前のフレーム境界で、投入済み GPU 処理の完了を待つ。 */
		[[nodiscard]] Result<void> WaitForIdle();
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
		/** Scene-linear grading, EV exposure and HDR bloom; applies on the next frame. */
		[[nodiscard]] Result<void> SetPostProcessSettings(const PostProcessSettings& settings);
		[[nodiscard]] PostProcessSettings GetPostProcessSettings() const noexcept;
		/**
		 * \brief Backend非依存Rendererを初期化してFrame記録経路へ接続する
		 * \param renderer Applicationが所有し、GraphicsSystemより長く生存するRenderer
		 */
		[[nodiscard]] Result<void> AttachRenderer(IGraphicsRenderer* renderer);
        /** \brief 記録開始前にGPU完了を待ち、初期化成功したRendererへ原子的に交換する */
        [[nodiscard]] Result<void> ReplaceRenderer(IGraphicsRenderer* renderer);
		/** \brief 上位描画拡張を初期化してPresent前の記録経路へ接続する */
		[[nodiscard]] Result<void> AttachRenderExtension(IGraphicsRenderExtension* extension);
		/** \brief 起動時GPU機能の値を返す。未初期化時は全機能が非対応 */
		[[nodiscard]] GraphicsCapabilities GetCapabilities() const noexcept;
		/** \brief Sceneの出力先をSwapChainから専用Viewport画像へ切り替える */
		[[nodiscard]] Result<void> EnableSceneViewport();
		/** \brief Panelサイズを次frameのリサイズ要求として保存する。記録中はGPU資源を変更しない */
		bool RequestSceneViewportSize(uint32_t width, uint32_t height) noexcept;
		/** \brief 描画snapshot抽出前にGPU idleを保証して未処理Resizeを適用する */
		[[nodiscard]] Result<void> PrepareSceneViewport();
		/** \brief UI backend専用の不透明画像参照。UIは資源を生成・解放しない */
		[[nodiscard]] ViewportImage GetSceneViewportImage() const noexcept;
		/** 独立したカメラプレビュー。作成・交換は描画命令の記録開始前に行う。 */
		[[nodiscard]] Result<void> ReplaceCameraPreviewRenderer(IGraphicsRenderer* renderer);
		/** GPU idle後にプレビュー資源を解放する。モデル差し替え時の旧Asset保持を防ぐ。 */
		[[nodiscard]] Result<void> ReleaseCameraPreviewRenderer();
		void SetCameraPreviewActive(bool active) noexcept;
		[[nodiscard]] ViewportImage GetCameraPreviewImage() const noexcept;

	private:
		class Impl;
		std::unique_ptr<Impl> impl_; //< DirectX 12具体実装の唯一の所有者
	};

} // namespace NexusEngine
