#pragma once

// c++
#include <cstdint>
#include <string>

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * WindowDetail
	 * - ウィンドウ生成時の設定値を所有する
	 * - Win32ハンドルや実行中の状態は管理しない
	 *---------------------------------------------------------------------------------------*/
	struct WindowDetail {
		std::wstring title		  = L"NexusEngine";
		uint32_t	 clientWidth  = 1280;
		uint32_t	 clientHeight = 720;
		bool		 resizable	  = true;
	};

} // namespace NexusEngine
