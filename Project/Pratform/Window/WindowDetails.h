#pragma once

// c++
#include <cstdint>

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * WindowDetail
	 * - ウィンドウの詳細情報を提供する
	 *---------------------------------------------------------------------------------------*/
	struct WindowDetail {
		const wchar_t*	  kWindowTitle_	 = L"NexusEngine";
		const int16_t kWindowWidth_	 = 1280;
		const int16_t kWindowHeight_ = 720;

		auto operator<=>(const WindowDetail& other) const noexcept = default;
	};

} // namespace NexusEngine