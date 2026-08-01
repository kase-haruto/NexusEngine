#include "ConsoleLogSink.h"

// c++
#include <cstdio>

// engine
#include "LogTextFormatter.h"

namespace NexusEngine {

	/////////////////////////////////////////////////////////////////////////////////////////
	//	コンソールへログを出力する
	/////////////////////////////////////////////////////////////////////////////////////////
	void ConsoleLogSink::Write(const LogRecord& record) noexcept {
		try {
			// Error以上だけstderrへ分け、通常ログとの用途を維持しながら外部ツールで検出しやすくする。
			const std::string text = FormatLogRecord(record);
			FILE* const stream = record.level >= LogLevel::Error ? stderr : stdout;
			static_cast<void>(std::fwrite(text.data(), sizeof(char), text.size(), stream));
			// クラッシュ直前のログも残るよう同期的にflushする。非同期化は将来のSinkで扱う。
			static_cast<void>(std::fflush(stream));
		} catch(...) {
			// ログ出力失敗をエンジン本体へ伝播させない。
		}
	}

} // namespace NexusEngine
