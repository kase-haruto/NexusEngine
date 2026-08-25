#pragma once

// c++
#include <cstdint>

// engine
#include "Foundation/Error/Result.h"
#include "Graphics/Renderer/GraphicsSystem.h"
#include "Platform/Window/ProjectWindow.h"

namespace NexusEngine {
	class IGraphicsRenderer;
	class IGraphicsRenderExtension;

	enum class FrameworkState : uint8_t {
		Uninitialized,
		Initializing,
		Running,
		ShuttingDown,
		Stopped,
		Failed
	};

	enum class FrameworkMode : uint8_t {
		Game,
		Editor,
		Headless
	};

	/*-----------------------------------------------------------------------------------------
	 * FrameworkDesc
	 * - Framework起動時のモードと各サブシステム設定を保持する
	 * - 実行中の状態やサブシステム自体は所有しない
	 *---------------------------------------------------------------------------------------*/
	struct FrameworkDesc {
		FrameworkMode	   mode = FrameworkMode::Game;
		WindowDetail	   window;
		GraphicsSystemDesc graphics;
		IGraphicsRenderer* renderer = nullptr; //< Application所有の通常描画Renderer
		IGraphicsRenderExtension* renderExtension = nullptr; //< Application所有の任意描画拡張
		ProjectWindow::MessageHandler messageHandler = nullptr; //< 任意の上位Window message購読関数
		void* messageHandlerUserData = nullptr; //< messageHandlerへ渡す非所有Context
	};

	/*-----------------------------------------------------------------------------------------
	 * NexusFramework
	 * - WindowとGraphicsDeviceを所有し、アプリケーション全体の寿命を制御する
	 * - 個別のDirectX初期化詳細や描画機能は管理しない
	 *---------------------------------------------------------------------------------------*/
	class NexusFramework {
	public:
		NexusFramework() noexcept = default;
		~NexusFramework() noexcept;
		NexusFramework(const NexusFramework&) = delete;
		NexusFramework& operator=(const NexusFramework&) = delete;
		NexusFramework(NexusFramework&&) = delete;
		NexusFramework& operator=(NexusFramework&&) = delete;

		/**
		 * \brief エンジンを初期化し、終了要求までフレームループを実行する
		 * \param desc 起動モードとサブシステム設定
		 * \return アプリケーション終了コード
		 */
		[[nodiscard]] int Run(const FrameworkDesc& desc = {}) noexcept;

		[[nodiscard]] FrameworkState GetState() const noexcept;

	private:
		[[nodiscard]] Result<void> Initialize(const FrameworkDesc& desc);
		void Shutdown() noexcept;
		[[nodiscard]] bool IsRunning() const noexcept;
		void BeginFrame() noexcept;
		[[nodiscard]] Result<void> ProcessEvents();
		void Update() noexcept;
		[[nodiscard]] Result<void> Render();
		void EndFrame() noexcept;

		ProjectWindow  window_;						   //< Gameモードのイベントと終了要求を所有するWindow
		GraphicsSystem graphicsSystem_;                 //< Graphicsのライフサイクルとフレーム描画の所有者
		FrameworkMode mode_  = FrameworkMode::Game;	   //< 現在実行中のFrameworkモード
		FrameworkState state_ = FrameworkState::Uninitialized; //< 不正遷移と二重終了を防ぐ状態
	};

} // namespace NexusEngine
