#include "LogTextFormatter.h"

// c++
#include <array>
#include <chrono>
#include <cstdio>

namespace NexusEngine {
	namespace {

		constexpr std::string_view ToString(const LogLevel level) noexcept {
			constexpr std::array names = {
				"Trace", "Debug", "Info", "Warning", "Error", "Critical"
			};
			return names.at(static_cast<size_t>(level));
		}

	} // namespace

	std::string FormatLogRecord(const LogRecord& record) {
		// Sink間で時刻やソース位置の表記がずれないよう、共通関数で一度の形式へ統一する。
		const auto time = std::chrono::system_clock::to_time_t(record.timestamp);
		tm localTime = {};
		localtime_s(&localTime, &time);

		std::array<char, 32> timeText = {};
		static_cast<void>(std::strftime(timeText.data(), timeText.size(), "%Y-%m-%d %H:%M:%S", &localTime));

		// 既知の固定部分を見込んでreserveし、短いログでの再確保回数を抑える。
		std::string text;
		text.reserve(record.message.size() + record.category.size() + 128);
		text.append("[");
		text.append(timeText.data());
		text.append("] [");
		text.append(ToString(record.level));
		text.append("] [");
		text.append(record.category);
		text.append("] ");
		text.append(record.message);
		text.append(" (");
		text.append(record.location.file_name());
		text.append(":");

		std::array<char, 16> lineText = {};
		const int length = std::snprintf(lineText.data(), lineText.size(), "%u", record.location.line());
		if(length > 0) {
			text.append(lineText.data(), static_cast<size_t>(length));
		}

		text.append(" ");
		text.append(record.location.function_name());
		text.append(")\n");
		return text;
	}

} // namespace NexusEngine
