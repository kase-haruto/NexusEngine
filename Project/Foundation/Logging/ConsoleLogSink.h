#pragma once

// engine
#include "ILogSink.h"

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * ConsoleLogSink
	 * - 整形したログを標準出力または標準エラーへ出力する
	 * - Windows固有のデバッグ出力は管理しない
	 *---------------------------------------------------------------------------------------*/
	class ConsoleLogSink final : public ILogSink {
	public:
		void Write(const LogRecord& record) noexcept override;
	};

} // namespace NexusEngine
