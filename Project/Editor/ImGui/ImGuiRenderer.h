#pragma once

#include <vector>

#include "Graphics/Descriptor/DescriptorHandle.h"
#include "Graphics/Renderer/GraphicsRenderExtension.h"
#include "Editor/Gui/Core/IGuiTextureProvider.h"

namespace NexusEngine {
	class DescriptorAllocator;

	/*-----------------------------------------------------------------------------------------
	 * ImGuiRenderer
	 * - Dear ImGui ContextとWin32/DX12 backendの初期化、フレーム、描画、終了を担当する
	 * - Device、Queue、Window、DescriptorAllocator本体は所有しない
	 *---------------------------------------------------------------------------------------*/
	class ImGuiRenderer final :
		public IGraphicsRenderExtension, public UI::ITextureProvider {
	public:
		static constexpr UI::TextureHandle SceneViewportTexture{1}; //< backend内だけで解決する安定token
		/** \brief Graphics所有のViewport画像をImGuiの不透明画像参照へ変換する */
		std::optional<UI::ResolvedTexture> Resolve(UI::TextureHandle texture) const noexcept override;
		/**
		 * \brief ImGui ContextとWin32/DX12 backendを初期化する
		 */
		[[nodiscard]] Result<void> Initialize(const GraphicsRenderExtensionContext& context) override;
		/**
		 * \brief ImGuiフレームの開始
		 */
		void BeginFrame() override;
		/**
		 * \brief ImGui描画コマンドをCommandListへ記録する
		 */
		void Record(GraphicsContext& context) override;
		/**
		 * \brief ImGui backendを終了する
		 */
		void Shutdown() noexcept override;
		/**
		 * \brief Win32 Window MessageをImGuiへ転送する
		 * \param window Window Handle
		 * \param message Window Message
		 * \param wParam Message固有の追加情報
		 * \param lParam Message固有の追加情報
		 * \return ImGuiがMessageを処理した場合true
		 */
		[[nodiscard]] bool HandleWindowMessage(void* window, uint32_t message, uintptr_t wParam, intptr_t lParam) noexcept;

	private:
		GraphicsSystem* graphicsSystem_ = nullptr; //< 画像解決先。資源を所有しない
		DescriptorAllocator*		  descriptors_ = nullptr; //< GraphicsSystem所有の共有Resource allocator
		std::vector<DescriptorHandle> allocations_;			  //< Backendへ貸し出したDescriptor
		bool						  initialized_ = false;	  //< Contextと両Backendの初期化完了状態
	};
} // namespace NexusEngine
