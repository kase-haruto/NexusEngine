#pragma once

#include <vector>
#include <map>
#include <memory>
#include <filesystem>
#include "ThirdParty/imgui/imgui.h"
#include "Graphics/Resource/WicImageDecoder.h"

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
		/** \brief Engine内蔵フォントの絶対パスを初期化前に指定する */
		void SetFontDirectory(std::filesystem::path directory) { fontDirectory_ = std::move(directory); }
		/** エンジン付属のPNGボタン画像のディレクトリを初期化前に指定する。 */
		void SetIconDirectory(std::filesystem::path directory) { iconDirectory_ = std::move(directory); }
		static constexpr UI::TextureHandle SceneViewportTexture{1}; //< backend内だけで解決する安定token
		static constexpr UI::TextureHandle CameraPreviewTexture{2}; //< カメラプレビュー専用の安定token
		/** \brief Graphics所有のViewport画像をImGuiの不透明画像参照へ変換する */
		std::optional<UI::ResolvedTexture> Resolve(UI::TextureHandle texture) const noexcept override;
        /** \brief Backend管理の動的TextureとしてPreview画像を登録する。GPU idle後のShutdownで解放 */
        ImTextureRef CreatePreview(const std::string& key, const DecodedImage& image);
        ImTextureRef FindPreview(const std::string& key) const;
        bool CanCreatePreview() const { return previews_.size() < 256; }
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
        void AfterSubmit() override;
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
		void LoadEditorIcons();
		std::map<std::string, std::unique_ptr<ImTextureData>> icons_;
		void DrawTitleBar();
		void DrawTitleBarCaption();
		void* nativeWindow_ = nullptr;
		std::filesystem::path fontDirectory_; //< GameのAsset Rootとは独立したUI資源
		std::filesystem::path iconDirectory_;
		std::vector<std::vector<char>> fontData_; //< 動的Glyph生成が参照するためContext終了まで保持する
		GraphicsSystem* graphicsSystem_ = nullptr; //< 画像解決先。資源を所有しない
        std::map<std::string, std::unique_ptr<ImTextureData>> previews_; //< Backend停止まで安定した参照を保持
		DescriptorAllocator*		  descriptors_ = nullptr; //< GraphicsSystem所有の共有Resource allocator
		std::vector<DescriptorHandle> allocations_;			  //< Backendへ貸し出したDescriptor
		bool						  initialized_ = false;	  //< Contextと両Backendの初期化完了状態
	};
} // namespace NexusEngine
