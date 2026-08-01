#pragma once

// c++
#include <string>

// engine
#include "LogRecord.h"

namespace NexusEngine {

	[[nodiscard]] std::string FormatLogRecord(const LogRecord& record);

} // namespace NexusEngine
