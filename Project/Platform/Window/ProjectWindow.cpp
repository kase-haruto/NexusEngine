#include "ProjectWindow.h"

// c++
#include <limits>
#include <string>

// engine
#include "Foundation/Logging/Logger.h"

namespace NexusEngine {
	namespace {

		constexpr int32_t kAlreadyInitialized = 1;
		constexpr int32_t kInvalidWindowSize  = 2;
		constexpr int32_t kRegisterClassFailed = 3;
		constexpr int32_t kAdjustRectFailed	 = 4;
		constexpr int32_t kCreateWindowFailed = 5;

		Error MakeWindowError(const int32_t code, std::string message) {
			// Win32 API呼び出し直後のLastErrorを保存し、後続のCleanupで上書きされる前に確定する。
			return Error(	
				ErrorCategory::Window,
				code,
				std::move(message),
				static_cast<Error::NativeErrorCode>(GetLastError()));
		}

	} // namespace

	ProjectWindow::~ProjectWindow() noexcept {
		Shutdown();
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	//	Win32ウィンドウを初期化する
	/////////////////////////////////////////////////////////////////////////////////////////
	Result<void> ProjectWindow::Initialize(const WindowDetail& detail) {
		// HWNDだけでなくクラス登録途中も初期化済みとみなし、二重登録を防止する。
		if(window_ != nullptr || classRegistered_) {
			return std::unexpected(Error(
				ErrorCategory::Window, kAlreadyInitialized, "Window is already initialized."));
		}

		// Win32のRECTへ安全に変換できない値はAPIへ渡す前に共通エラーへ変換する。
		if(detail.clientWidth == 0 || detail.clientHeight == 0 ||
		   detail.clientWidth > static_cast<uint32_t>(std::numeric_limits<LONG>::max()) ||
		   detail.clientHeight > static_cast<uint32_t>(std::numeric_limits<LONG>::max())) {
			return std::unexpected(Error(
				ErrorCategory::Window, kInvalidWindowSize, "Window client size is invalid."));
		}

		// 実行モジュールをWindow classの所有元として使用し、Shutdown時も同じ値で登録解除する。
		instance_ = GetModuleHandleW(nullptr);
		className_ = L"NexusEngine.ProjectWindow";

		// static WindowProcedureを登録し、WM_NCCREATEで個別インスタンスへ接続する。
		WNDCLASSEXW windowClass = {};
		windowClass.cbSize		  = sizeof(WNDCLASSEXW);
		windowClass.style		  = CS_HREDRAW | CS_VREDRAW;
		windowClass.lpfnWndProc	  = &ProjectWindow::WindowProcedure;
		windowClass.hInstance	  = instance_;
		windowClass.hCursor		  = LoadCursorW(nullptr, IDC_ARROW);
		windowClass.lpszClassName  = className_.c_str();

		if(RegisterClassExW(&windowClass) == 0) {
			return std::unexpected(MakeWindowError(kRegisterClassFailed, "Failed to register window class."));
		}
		// ここ以降の失敗ではShutdownがUnregisterClassWを実行できるよう、直ちに状態を記録する。
		classRegistered_ = true;

		// resizable=falseでも閉じる、最小化するという基本操作は残す。
		const DWORD style = detail.resizable
			? WS_OVERLAPPEDWINDOW
			: WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
		RECT windowRect = {
			0,
			0,
			static_cast<LONG>(detail.clientWidth),
			static_cast<LONG>(detail.clientHeight)
		};

		// 要求値はクライアント領域なので、タイトルバーと枠を含む外側サイズへ変換する。
		if(!AdjustWindowRectEx(&windowRect, style, FALSE, 0)) {
			Error error = MakeWindowError(kAdjustRectFailed, "Failed to calculate window size.");
			// クラス登録まで成功しているため、エラー返却前に登録だけを巻き戻す。
			Shutdown();
			return std::unexpected(std::move(error));
		}

		// lpParamへthisを渡し、最初のWM_NCCREATEからメンバー関数へメッセージを配送する。
		window_ = CreateWindowExW(
			0,
			className_.c_str(),
			detail.title.c_str(),
			style,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			windowRect.right - windowRect.left,
			windowRect.bottom - windowRect.top,
			nullptr,
			nullptr,
			instance_,
			this);

		if(window_ == nullptr) {
			Error error = MakeWindowError(kCreateWindowFailed, "Failed to create project window.");
			// HWND生成失敗時も登録済みWindow classを残さず、再初期化可能な状態へ戻す。
			Shutdown();
			return std::unexpected(std::move(error));
		}

		// HWNDを完全に生成してからrunningを公開し、不完全なWindowでループが始まるのを防ぐ。
		running_ = true;
		ShowWindow(window_, SW_SHOW);
		UpdateWindow(window_);
		NEXUS_LOG_INFO("Window", "Project window initialized.");
		return {};
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	//	ウィンドウを破棄する
	/////////////////////////////////////////////////////////////////////////////////////////
	void ProjectWindow::Shutdown() noexcept {
		// 破棄処理中にメッセージが配送されても、次フレームへ進まない状態を先に確定する。
		running_ = false;

		// HWNDを先に破棄してからクラス登録を解除し、Win32の寿命順序を保証する。
		if(window_ != nullptr) {
			// DestroyWindowは同期的にWM_DESTROYを呼び、HandleMessage側でwindow_をnullptrにする。
			DestroyWindow(window_);
			window_ = nullptr;
		}

		if(classRegistered_) {
			// このクラスから生成したHWNDがなくなった後でのみWindow classを登録解除する。
			UnregisterClassW(className_.c_str(), instance_);
			classRegistered_ = false;
		}

		className_.clear();
		instance_ = nullptr;
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	//	Win32メッセージを処理する
	/////////////////////////////////////////////////////////////////////////////////////////
	bool ProjectWindow::ProcessEvents() noexcept {
		MSG message = {};
		// ブロッキングするGetMessageは使わず、将来のリアルタイム更新を止めない。
		// 1フレーム内に溜まったメッセージを全件処理し、入力遅延の蓄積を避ける。
		while(PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
			if(message.message == WM_QUIT) {
				// WM_QUITは通常のWindowProcedureへ配送されないため、ここで実行状態へ反映する。
				running_ = false;
				continue;
			}
			// キーボード文字変換を行ってから、登録済みWindowProcedureへメッセージを配送する。
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}
		return running_;
	}

	bool ProjectWindow::IsRunning() const noexcept {
		return running_;
	}

	HWND ProjectWindow::GetNativeHandle() const noexcept {
		return window_;
	}

	LRESULT CALLBACK ProjectWindow::WindowProcedure(
		const HWND window,
		const UINT message,
		const WPARAM wParam,
		const LPARAM lParam) noexcept {
		ProjectWindow* projectWindow = nullptr;

		if(message == WM_NCCREATE) {
			// HWNDにProjectWindowポインタを一度だけ関連付け、以後のメッセージを個体へ転送する。
			const auto* const create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
			projectWindow = static_cast<ProjectWindow*>(create->lpCreateParams);
			SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(projectWindow));
		} else {
			projectWindow = reinterpret_cast<ProjectWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
		}

		if(projectWindow != nullptr) {
			// static callbackにWin32依存を留め、状態変更はインスタンス側へ集約する。
			return projectWindow->HandleMessage(window, message, wParam, lParam);
		}
		return DefWindowProcW(window, message, wParam, lParam);
	}

	LRESULT ProjectWindow::HandleMessage(
		const HWND window,
		const UINT message,
		const WPARAM wParam,
		const LPARAM lParam) noexcept {
		switch(message) {
		case WM_CLOSE:
			// 終了要求を先に公開してからHWNDを破棄し、同じフレームのUpdate実行を抑止する。
			running_ = false;
			DestroyWindow(window);
			return 0;
		case WM_DESTROY:
			// 所有ハンドルを無効化し、Shutdownから同じHWNDを二重破棄しないようにする。
			window_ = nullptr;
			running_ = false;
			// スレッドのメッセージキューへWM_QUITを投入し、ProcessEvents側にも終了を伝える。
			PostQuitMessage(0);
			return 0;
		default:
			return DefWindowProcW(window, message, wParam, lParam);
		}
	}

} // namespace NexusEngine
