#pragma once

// engine
#include "ILogSink.h"

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * DebugOutputLogSink
	 * - Visual Studioのデバッグ出力へログを送るWindows専用Sink
	 * - 利用側からWindows API依存を隔離する
	 *---------------------------------------------------------------------------------------*/
	class DebugOutputLogSink final : public ILogSink {
	public:
		void Write(const LogRecord& record) noexcept override;
	};

} // namespace NexusEngine
