#include "ProjectWindow.h"


/////////////////////////////////////////////////////////////////////////////////////////
//	windowの初期化
/////////////////////////////////////////////////////////////////////////////////////////
void NexusEngine::ProjectWindow::Initialize(const WindowDetail& detail) {

	// windowサイズを表す構造体に設定を入れる
	rect_ = {0, 0, detail.kWindowWidth_, detail.kWindowHeight_};

	// クライアント領域をもとの実際のサイズにwrcを変更してもらう
	AdjustWindowRect(&rect_, WS_OVERLAPPEDWINDOW, false);

	// windowの生成
	HWND hwnd = CreateWindow(
		detail.kWindowTitle_, detail.kWindowTitle_, WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, rect_.right - rect_.left, rect_.bottom - rect_.top,
		nullptr, nullptr, nullptr, nullptr);
}
