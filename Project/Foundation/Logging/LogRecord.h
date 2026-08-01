#pragma once

// c++
#include <chrono>
#include <cstdint>
#include <source_location>
#include <string_view>

namespace NexusEngine {

	enum class LogLevel : uint8_t {
		Trace,
		Debug,
		Info,
		Warning,
		Error,
		Critical
	};

	/*-----------------------------------------------------------------------------------------
	 * LogRecord
	 * - Sinkへ同期的に渡す一件分のログ情報を表す
	 * - 文字列の所有権はLogger呼び出し側にあり、Sinkは保持しない
	 *---------------------------------------------------------------------------------------*/
	struct LogRecord {
		LogLevel								 level	  = LogLevel::Info;
		std::string_view						 category;
		std::string_view						 message;
		std::chrono::system_clock::time_point timestamp;
		std::source_location					 location;
	};

} // namespace NexusEngine
