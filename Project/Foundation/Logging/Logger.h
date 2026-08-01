#pragma once

// c++
#include <array>
#include <memory>
#include <mutex>
#include <source_location>
#include <string_view>

// engine
#include "ILogSink.h"

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * Logger
	 * - エンジン共通の同期ログ配送とSink所有を担当する
	 * - 個別の出力形式やWindows API呼び出しは各Sinkへ委譲する
	 *---------------------------------------------------------------------------------------*/
	class Logger final {
	public:
		static constexpr size_t kMaxSinkCount = 8;

		Logger() noexcept = default;
		~Logger() noexcept = default;
		Logger(const Logger&) = delete;
		Logger& operator=(const Logger&) = delete;
		Logger(Logger&&) = delete;
		Logger& operator=(Logger&&) = delete;

		[[nodiscard]] static Logger& Get() noexcept;

		/**
		 * \brief 既定のConsoleとDebugOutput Sinkを登録する
		 * \note 複数回呼び出してもSinkは重複登録しない
		 */
		void InitializeDefaultSinks() noexcept;

		/**
		 * \brief ログ出力先を追加する
		 * \param sink Loggerが所有する出力先
		 * \return 登録できた場合true
		 */
		[[nodiscard]] bool AddSink(std::unique_ptr<ILogSink> sink) noexcept;

		/**
		 * \brief 全Sinkへログを同期配送する
		 * \param level ログレベル
		 * \param category ログカテゴリ
		 * \param message ログ本文
		 * \param location 呼び出し元位置
		 */
		void Log(
			LogLevel level,
			std::string_view category,
			std::string_view message,
			std::source_location location = std::source_location::current()) noexcept;

	private:
		std::mutex										 mutex_;		 //< Sink登録と同期出力を直列化する
		std::array<std::unique_ptr<ILogSink>, kMaxSinkCount> sinks_;		 //< 上限付きで所有する出力先
		size_t											 sinkCount_ = 0; //< 登録済みSink数
		bool											 initialized_ = false; //< 既定Sinkの登録状態
	};

} // namespace NexusEngine

#define NEXUS_LOG_TRACE(category, message) \
	::NexusEngine::Logger::Get().Log(::NexusEngine::LogLevel::Trace, category, message, std::source_location::current())
#define NEXUS_LOG_DEBUG(category, message) \
	::NexusEngine::Logger::Get().Log(::NexusEngine::LogLevel::Debug, category, message, std::source_location::current())
#define NEXUS_LOG_INFO(category, message) \
	::NexusEngine::Logger::Get().Log(::NexusEngine::LogLevel::Info, category, message, std::source_location::current())
#define NEXUS_LOG_WARNING(category, message) \
	::NexusEngine::Logger::Get().Log(::NexusEngine::LogLevel::Warning, category, message, std::source_location::current())
#define NEXUS_LOG_ERROR(category, message) \
	::NexusEngine::Logger::Get().Log(::NexusEngine::LogLevel::Error, category, message, std::source_location::current())
#define NEXUS_LOG_CRITICAL(category, message) \
	::NexusEngine::Logger::Get().Log(::NexusEngine::LogLevel::Critical, category, message, std::source_location::current())
