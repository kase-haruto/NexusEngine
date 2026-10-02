#pragma once

#include "Editor/Gui/Core/GuiContext.h"
#include "Editor/ImGui/ImGuiRenderer.h"

namespace NexusEngine {
	/*-----------------------------------------------------------------------------------------
	 * EditorApplication
	 * - Editor UIの構築とImGui描画基盤の接続を担当する
	 * - Win32/DX12固有処理はImGuiRendererへ委譲する
	 *---------------------------------------------------------------------------------------*/
	class EditorApplication final : public IGraphicsRenderExtension {
	public:
		/**
		 * @brief 初期化
		 * @param context
		 * @return 失敗した場合の原因
		 */
		[[nodiscard]] Result<void> Initialize(const GraphicsRenderExtensionContext& context) override;
		/**
		 * @brief 開始フレーム
		 */
		void BeginFrame() override;
		/**
		 * @brief commandListに記録する
		 * @param context
		 */
		void Record(GraphicsContext& context) override;
		/**
		 * @brief 停止
		 */
		void Shutdown() noexcept override;
		/**
		 * @brief  Win32 Window MessageをImGuiへ転送する
		 * @param window
		 * @param message
		 * @param wParam
		 * @param lParam
		 * @return
		 */
		[[nodiscard]] bool HandleWindowMessage(
			void*	  window,
			uint32_t  message,
			uintptr_t wParam,
			intptr_t  lParam) noexcept;

	private:
		void DrawDockSpace();
		void DrawMainMenuBar();

		ImGuiRenderer imguiRenderer_; //< imgui描画
		UI::Context	  guiContext_;	  //< gui
		bool		  showDemoWindow_ = false;
	};
} // namespace NexusEngine
