#include "DebugOutputLogSink.h"

// c++
#include <Windows.h>

// engine
#include "LogTextFormatter.h"

namespace NexusEngine {

	/////////////////////////////////////////////////////////////////////////////////////////
	//	Visual Studioのデバッグ出力へログを送る
	/////////////////////////////////////////////////////////////////////////////////////////
	void DebugOutputLogSink::Write(const LogRecord& record) noexcept {
		try {
			// Windows API依存はこのSink内だけに閉じ、Logger利用側をPlatform非依存に保つ。
			const std::string text = FormatLogRecord(record);
			OutputDebugStringA(text.c_str());
		} catch(...) {
			// デバッグ出力の失敗によってエンジンを停止させない。
		}
	}

} // namespace NexusEngine
