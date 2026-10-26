#pragma once
#include "Editor/Gui/Core/IGuiTextureProvider.h"
#include "Foundation/IPC/RuntimeProtocol.h"
#include "Graphics/Viewport/IViewportFrameTransport.h"
#include "Runtime/Scene/Components/PersistentIdComponent.h"
#include <functional>

namespace NexusEngine {
	/*-----------------------------------------------------------------------------------------
	 * ViewportSession
	 * - Viewport UIとControl / Frame Channelの間を仲介する
	 * - 接続と転送実装はhost所有。GPU submission後のlease解放はhostが行う
	 *---------------------------------------------------------------------------------------*/
	class ViewportSession final {
	public:
		/** \brief hostの非所有接続を設定する。切断済みの状態でのみ接続する */
		bool Attach(IPC::IRuntimeConnection& connection, IViewportFrameTransport& frames, UI::ITextureProvider& textures);
		/** \brief 既存同一processの実描画を接続する。Remote接続済みを装わない */
		bool AttachLocalPreview(UI::ITextureProvider& textures, UI::TextureHandle texture,
								std::function<bool(uint32_t, uint32_t)> resize);
		bool IsLocalPreview() const noexcept { return localTextures_ != nullptr; }
		/** \brief hostが描画開始前に完成frameを取得する */
		void BeginFrame();
		/** \brief hostがsampling command提出後に必ず呼ぶ。未提出時はframeを保持する */
		bool CompleteSubmission();
		/** \brief hostがGPU idleを保証した後に切り離す */
		void Detach() noexcept;
		/** \brief 同一サイズの重複通知を除去してResize要求を送る */
		bool Resize(uint32_t width, uint32_t height);
		/** \brief viewport相対入力を登録IPCへ送る */
		bool Input(uint32_t kind, uint32_t code, float x, float y);
		/** \brief 選択IDをControl Channelへ送る */
		bool							   Select(PersistentId id);
		IPC::RuntimeState				   State() const { return connection_ ? connection_->State() : IPC::RuntimeState::Disconnected; }
		std::optional<UI::ResolvedTexture> Image() const;
		void							   SetCameraPreviewTexture(UI::TextureHandle texture) { cameraPreviewTexture_ = texture; }
		std::optional<UI::ResolvedTexture> CameraPreviewImage() const {
			return localTextures_ ? localTextures_->Resolve(cameraPreviewTexture_) : std::nullopt;
		}
		void SetCameraPreviewVisible(bool visible) noexcept { cameraPreviewVisible_ = visible; }
		bool IsCameraPreviewVisible() const noexcept { return cameraPreviewVisible_; }

	private:
		UI::TextureHandle						cameraPreviewTexture_;
		bool									cameraPreviewVisible_ = false;
		IPC::IRuntimeConnection*				connection_			  = nullptr; //< host所有
		IViewportFrameTransport*				frames_				  = nullptr; //< Graphics host所有
		UI::ITextureProvider*					textures_			  = nullptr; //< ImGui adapter所有
		std::optional<ViewportFrame>			frame_;							 //< submission完了まで有効なlease
		uint32_t								width_ = 0, height_ = 0;		 //< 最後に通知したサイズ
		uint64_t								generation_	   = 0;				 //< Resize世代
		UI::ITextureProvider*					localTextures_ = nullptr;		 //< 同一process host所有の画像解決先
		UI::TextureHandle						localTexture_;					 //< GPU型やdescriptorを保持しない
		std::function<bool(uint32_t, uint32_t)> localResize_;					 //< 次frameへのResize要求だけを行う
	};
} // namespace NexusEngine
