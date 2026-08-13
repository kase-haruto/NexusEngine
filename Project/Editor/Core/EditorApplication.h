#pragma once

#include "Editor/ImGui/ImGuiRenderer.h"
#include "Editor/Gui/Core/GuiContext.h"

namespace NexusEngine {
	/*-----------------------------------------------------------------------------------------
	 * EditorApplication
	 * - Editor UIの構築とImGui描画基盤の接続を担当する
	 * - Win32/DX12固有処理はImGuiRendererへ委譲する
	 *---------------------------------------------------------------------------------------*/
	class EditorApplication final :
		public IGraphicsRenderExtension {
	public:
		[[nodiscard]] Result<void> Initialize(const GraphicsRenderExtensionContext& context) override;
		void BeginFrame() override;
		void Record(ID3D12GraphicsCommandList* commandList) override;
		void Shutdown() noexcept override;

		[[nodiscard]] bool HandleWindowMessage(
			void* window,
			uint32_t message,
			uintptr_t wParam,
			intptr_t lParam) noexcept;

	private:
		void DrawDockSpace();
		void DrawMainMenuBar();

		ImGuiRenderer imguiRenderer_;
		UI::Context guiContext_;
		bool showDemoWindow_ = false;
	};
} // namespace NexusEngine
