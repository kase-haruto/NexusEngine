#pragma once

// engine
#include "LogRecord.h"

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * ILogSink
	 * - Loggerから受け取ったログの出力先を抽象化する
	 * - ログのフィルタリングや保管は管理しない
	 *---------------------------------------------------------------------------------------*/
	class ILogSink {
	public:
		virtual ~ILogSink() noexcept = default;
		virtual void Write(const LogRecord& record) noexcept = 0;
	};

} // namespace NexusEngine
