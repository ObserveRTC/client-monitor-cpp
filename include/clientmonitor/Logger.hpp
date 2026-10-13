/**
 * Logger facility.
 *
 * Mirrors libmediasoupclient's Logger.hpp. Each source file including
 * Logger.hpp MUST define its own CM_CLASS macro. Include files (.hpp) MUST NOT
 * include Logger.hpp.
 *
 * All the logging macros use the same format as printf().
 *
 * CM_TRACE()        Logs the current method if CM_LOG_TRACE is defined and the
 *                   log level is LOG_TRACE.
 * CM_DEBUG(...)     Logs a debug.
 * CM_WARN(...)      Logs a warning.
 * CM_ERROR(...)     Logs an error.
 * CM_DUMP(...)      Logs always to stdout.
 * CM_ABORT(...)     Logs the given error to stderr and aborts the process.
 * CM_ASSERT(c, ...) If the condition is not satisfied, it calls CM_ABORT().
 */

#ifndef CM_LOGGER_HPP
#define CM_LOGGER_HPP

#include <cstdint> // uint8_t
#include <cstdio>  // std::snprintf(), std::fprintf(), stdout, stderr
#include <cstdlib> // std::abort()
#include <cstring>
#include <mutex>

namespace observertc::clientmonitor
{
	class Logger
	{
	public:
		enum class LogLevel : uint8_t
		{
			LOG_NONE  = 0,
			LOG_ERROR = 1,
			LOG_WARN  = 2,
			LOG_DEBUG = 3,
			LOG_TRACE = 4
		};

		class LogHandlerInterface
		{
		public:
			virtual ~LogHandlerInterface()                                = default;
			virtual void OnLog(LogLevel level, char* payload, size_t len) = 0;
		};

		class DefaultLogHandler : public LogHandlerInterface
		{
			void OnLog(LogLevel level, char* payload, size_t len) override;
		};

		static void SetLogLevel(LogLevel level);
		static void SetHandler(LogHandlerInterface* handler);
		static void SetDefaultHandler();

	public:
		static LogLevel logLevel;
		static LogHandlerInterface* handler;
		static const size_t BufferSize{ 50000 };
		static char buffer[];
		// The collecting timer runs on its own thread, so the shared buffer is guarded.
		static std::mutex mutex;
	};
} // namespace observertc::clientmonitor

// clang-format off

/* Logging macros. */

// NOLINTBEGIN
#define _CM_LOG_SEPARATOR_CHAR "\n"

#ifdef CM_LOG_FILE_LINE
	#define _CM_LOG_STR " %s:%d | %s::%s()"
	#define _CM_LOG_STR_DESC _CM_LOG_STR " | "
	#define _CM_FILE (std::strchr(__FILE__, '/') ? std::strchr(__FILE__, '/') + 1 : __FILE__)
	#define _CM_LOG_ARG _CM_FILE, __LINE__, CM_CLASS, __FUNCTION__
#else
	#define _CM_LOG_STR " %s::%s()"
	#define _CM_LOG_STR_DESC _CM_LOG_STR " | "
	#define _CM_LOG_ARG CM_CLASS, __FUNCTION__
#endif

// The format string is the first of __VA_ARGS__ (no GNU ##__VA_ARGS__, which -Wpedantic flags on
// Clang): the "[TAG] Class::Method() | " prefix is printed first and the caller's text appended.
#define _CM_LOG(level, tag, ...) \
	do \
	{ \
		if (observertc::clientmonitor::Logger::handler && observertc::clientmonitor::Logger::logLevel >= observertc::clientmonitor::Logger::LogLevel::level) \
		{ \
			std::lock_guard<std::mutex> loggerLock(observertc::clientmonitor::Logger::mutex); \
			int loggerWritten = std::snprintf(observertc::clientmonitor::Logger::buffer, observertc::clientmonitor::Logger::BufferSize, "[" tag "]" _CM_LOG_STR_DESC, _CM_LOG_ARG); \
			if (loggerWritten < 0) loggerWritten = 0; \
			if (static_cast<size_t>(loggerWritten) < observertc::clientmonitor::Logger::BufferSize) \
			{ \
				const int loggerAppended = std::snprintf(observertc::clientmonitor::Logger::buffer + loggerWritten, observertc::clientmonitor::Logger::BufferSize - static_cast<size_t>(loggerWritten), __VA_ARGS__); \
				if (loggerAppended > 0) loggerWritten += loggerAppended; \
			} \
			if (static_cast<size_t>(loggerWritten) >= observertc::clientmonitor::Logger::BufferSize) loggerWritten = observertc::clientmonitor::Logger::BufferSize - 1; \
			observertc::clientmonitor::Logger::handler->OnLog(observertc::clientmonitor::Logger::LogLevel::level, observertc::clientmonitor::Logger::buffer, static_cast<size_t>(loggerWritten)); \
		} \
	} \
	while (false)
// NOLINTEND

#ifdef CM_LOG_TRACE
	#define CM_TRACE() \
		do \
		{ \
			if (observertc::clientmonitor::Logger::handler && observertc::clientmonitor::Logger::logLevel == observertc::clientmonitor::Logger::LogLevel::LOG_TRACE) \
			{ \
				std::lock_guard<std::mutex> loggerLock(observertc::clientmonitor::Logger::mutex); \
				int loggerWritten = std::snprintf(observertc::clientmonitor::Logger::buffer, observertc::clientmonitor::Logger::BufferSize, "[TRACE]" _CM_LOG_STR, _CM_LOG_ARG); \
				observertc::clientmonitor::Logger::handler->OnLog(observertc::clientmonitor::Logger::LogLevel::LOG_TRACE, observertc::clientmonitor::Logger::buffer, loggerWritten); \
			} \
		} \
		while (false)
#else
	#define CM_TRACE() ;
#endif

#define CM_DEBUG(...) _CM_LOG(LOG_DEBUG, "DEBUG", __VA_ARGS__)
#define CM_WARN(...) _CM_LOG(LOG_WARN, "WARN", __VA_ARGS__)
#define CM_ERROR(...) _CM_LOG(LOG_ERROR, "ERROR", __VA_ARGS__)

#define CM_DUMP(...) \
	do \
	{ \
		std::fprintf(stdout, _CM_LOG_STR_DESC, _CM_LOG_ARG); \
		std::fprintf(stdout, __VA_ARGS__); \
		std::fprintf(stdout, _CM_LOG_SEPARATOR_CHAR); \
		std::fflush(stdout); \
	} \
	while (false)

#define CM_ABORT(...) \
	do \
	{ \
		std::fprintf(stderr, "[ABORT]" _CM_LOG_STR_DESC, _CM_LOG_ARG); \
		std::fprintf(stderr, __VA_ARGS__); \
		std::fprintf(stderr, _CM_LOG_SEPARATOR_CHAR); \
		std::fflush(stderr); \
		std::abort(); \
	} \
	while (false)

#define CM_ASSERT(condition, ...) \
	if (!(condition)) \
	{ \
		std::fprintf(stderr, "[ABORT]" _CM_LOG_STR_DESC "failed assertion '%s': ", _CM_LOG_ARG, #condition); \
		std::fprintf(stderr, __VA_ARGS__); \
		std::fprintf(stderr, _CM_LOG_SEPARATOR_CHAR); \
		std::fflush(stderr); \
		std::abort(); \
	}

// clang-format on

#endif
