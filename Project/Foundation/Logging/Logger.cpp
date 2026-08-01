#include "Logger.h"

// c++
#include <chrono>
#include <utility>

// engine
#include "ConsoleLogSink.h"
#include "DebugOutputLogSink.h"

namespace NexusEngine {

	Logger& Logger::Get() noexcept {
		// 関数ローカルstaticにより、初回利用時のスレッド安全な初期化と終了時破棄を利用する。
		static Logger instance;
		return instance;
	}

	void Logger::InitializeDefaultSinks() noexcept {
		try {
			// 両方の生成が完了してから公開し、途中失敗による重複登録を防止する。
			auto consoleSink = std::make_unique<ConsoleLogSink>();
			auto debugSink = std::make_unique<DebugOutputLogSink>();

			std::scoped_lock lock(mutex_);
			if(initialized_) {
				return;
			}
			sinks_[sinkCount_++] = std::move(consoleSink);
			sinks_[sinkCount_++] = std::move(debugSink);
			initialized_ = true;
		} catch(...) {
			// 初期化前後を問わず、登録済みのSinkだけで継続できる状態を保つ。
		}
	}

	bool Logger::AddSink(std::unique_ptr<ILogSink> sink) noexcept {
		// nullptr登録を拒否し、Log側で毎回のnull確認を不要にする。
		if(!sink) {
			return false;
		}

		std::scoped_lock lock(mutex_);
		// 固定上限を超えた場合は再確保せず失敗させ、ログ経路のメモリ特性を一定に保つ。
		if(sinkCount_ >= sinks_.size()) {
			return false;
		}

		sinks_[sinkCount_++] = std::move(sink);
		return true;
	}

	void Logger::Log(
		const LogLevel level,
		const std::string_view category,
		const std::string_view message,
		const std::source_location location) noexcept {
#if defined(NDEBUG) && !defined(NEXUS_DEVELOP)
		// Releaseでは低レベルログをSinkへ渡す前に除外し、時刻取得や整形コストも発生させない。
		if(level == LogLevel::Trace || level == LogLevel::Debug) {
			return;
		}
#endif

		try {
			// LogRecordはstring_viewで呼び出し元文字列を参照するが、全Sinkへ同期配送する間だけ使用する。
			const LogRecord record {
				.level = level,
				.category = category,
				.message = message,
				.timestamp = std::chrono::system_clock::now(),
				.location = location
			};

			// 一件のログを全Sinkへ連続配送し、複数スレッドの行が途中で混ざることを防ぐ。
			std::scoped_lock lock(mutex_);
			for(size_t index = 0; index < sinkCount_; ++index) {
				sinks_[index]->Write(record);
			}
		} catch(...) {
			// Loggerはnoexcept境界とし、出力障害をゲームループへ波及させない。
		}
	}

} // namespace NexusEngine
