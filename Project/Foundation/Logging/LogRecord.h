#pragma once

// c++
#include <chrono>
#include <cstdint>
#include <source_location>
#include <string_view>

namespace NexusEngine {

	enum class LogLevel : uint8_t {
		Trace,	 //< 低レベルの詳細ログ。通常は無効化する
		Debug,	 //< デバッグ用ログ。通常は無効化する
		Info,	 //< 通常の情報ログ。ユーザーに通知する場合もある
		Warning, //< 注意を要するログ。ユーザーに通知する場合もある
		Error,	 //< エラー発生ログ。ユーザーに通知する場合もある
		Critical //< 重大なエラー発生ログ。ユーザーに通知する場合もある
	};

	/*-----------------------------------------------------------------------------------------
	 * LogRecord
	 * - Sinkへ同期的に渡す一件分のログ情報を表す
	 * - 文字列の所有権はLogger呼び出し側にあり、Sinkは保持しない
	 *---------------------------------------------------------------------------------------*/
	struct LogRecord {
		LogLevel							  level = LogLevel::Info;
		std::string_view					  category;
		std::string_view					  message;
		std::chrono::system_clock::time_point timestamp;
		std::source_location				  location;
	};

} // namespace NexusEngine
