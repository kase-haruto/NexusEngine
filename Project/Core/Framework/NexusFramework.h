#pragma once

// c++
#include <cstdint>

// engine
#include "Foundation/Error/Result.h"
#include "Graphics/Device/GraphicsDevice.h"
#include "Platform/Window/ProjectWindow.h"

namespace NexusEngine {

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
		GraphicsDeviceDesc graphics;
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
		void ProcessEvents() noexcept;
		void Update() noexcept;
		void Render() noexcept;
		void EndFrame() noexcept;

		ProjectWindow  window_;						   //< Gameモードのイベントと終了要求を所有するWindow
		GraphicsDevice graphicsDevice_;				   //< Factory、Adapter、D3D12 Deviceの所有者
		FrameworkMode mode_  = FrameworkMode::Game;	   //< 現在実行中のFrameworkモード
		FrameworkState state_ = FrameworkState::Uninitialized; //< 不正遷移と二重終了を防ぐ状態
	};

} // namespace NexusEngine
