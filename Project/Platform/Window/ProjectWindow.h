#pragma once

// c++
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

// engine
#include "Foundation/Error/Result.h"
#include "WindowDetails.h"

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * ProjectWindow
	 * - Win32ウィンドウクラスとHWNDの生成、イベント処理、破棄を担当する
	 * - Graphics DeviceやSwapChainは所有しない
	 *---------------------------------------------------------------------------------------*/
	class ProjectWindow {
	public:
		ProjectWindow() noexcept = default;
		~ProjectWindow() noexcept;
		ProjectWindow(const ProjectWindow&) = delete;
		ProjectWindow& operator=(const ProjectWindow&) = delete;
		ProjectWindow(ProjectWindow&&) = delete;
		ProjectWindow& operator=(ProjectWindow&&) = delete;

		/**
		 * \brief ウィンドウクラスを登録してウィンドウを生成する
		 * \param detail ウィンドウの設定
		 * \return 初期化結果
		 */
		[[nodiscard]] Result<void> Initialize(const WindowDetail& detail = {});

		/**
		 * \brief 保持しているウィンドウと登録クラスを安全に破棄する
		 */
		void Shutdown() noexcept;

		/**
		 * \brief キューにあるWin32メッセージを処理する
		 * \return 終了要求を受けていない場合true
		 */
		[[nodiscard]] bool ProcessEvents() noexcept;

		[[nodiscard]] bool IsRunning() const noexcept;
		[[nodiscard]] HWND GetNativeHandle() const noexcept;

	private:
		static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
		LRESULT HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;

		HINSTANCE instance_		   = nullptr; //< クラス登録とウィンドウ生成に使用するモジュール
		HWND	  window_		   = nullptr; //< ProjectWindowが所有するネイティブウィンドウ
		std::wstring className_;			   //< 登録解除まで保持する一意なクラス名
		bool	  classRegistered_ = false;	 //< 部分初期化時の登録解除判定
		bool	  running_		   = false;	 //< WM_CLOSEまたはWM_QUITを反映する実行状態
	};

} // namespace NexusEngine
