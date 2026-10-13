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

namespace clientmonitor
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
} // namespace clientmonitor

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

#define _CM_LOG(level, tag, desc, ...) \
	do \
	{ \
		if (clientmonitor::Logger::handler && clientmonitor::Logger::logLevel >= clientmonitor::Logger::LogLevel::level) \
		{ \
			std::lock_guard<std::mutex> loggerLock(clientmonitor::Logger::mutex); \
			int loggerWritten = std::snprintf(clientmonitor::Logger::buffer, clientmonitor::Logger::BufferSize, "[" tag "]" _CM_LOG_STR_DESC desc, _CM_LOG_ARG, ##__VA_ARGS__); \
			if (loggerWritten < 0) loggerWritten = 0; \
			if (static_cast<size_t>(loggerWritten) >= clientmonitor::Logger::BufferSize) loggerWritten = clientmonitor::Logger::BufferSize - 1; \
			clientmonitor::Logger::handler->OnLog(clientmonitor::Logger::LogLevel::level, clientmonitor::Logger::buffer, loggerWritten); \
		} \
	} \
	while (false)
// NOLINTEND

#ifdef CM_LOG_TRACE
	#define CM_TRACE() \
		do \
		{ \
			if (clientmonitor::Logger::handler && clientmonitor::Logger::logLevel == clientmonitor::Logger::LogLevel::LOG_TRACE) \
			{ \
				std::lock_guard<std::mutex> loggerLock(clientmonitor::Logger::mutex); \
				int loggerWritten = std::snprintf(clientmonitor::Logger::buffer, clientmonitor::Logger::BufferSize, "[TRACE]" _CM_LOG_STR, _CM_LOG_ARG); \
				clientmonitor::Logger::handler->OnLog(clientmonitor::Logger::LogLevel::LOG_TRACE, clientmonitor::Logger::buffer, loggerWritten); \
			} \
		} \
		while (false)
#else
	#define CM_TRACE() ;
#endif

#define CM_DEBUG(desc, ...) _CM_LOG(LOG_DEBUG, "DEBUG", desc, ##__VA_ARGS__)
#define CM_WARN(desc, ...) _CM_LOG(LOG_WARN, "WARN", desc, ##__VA_ARGS__)
#define CM_ERROR(desc, ...) _CM_LOG(LOG_ERROR, "ERROR", desc, ##__VA_ARGS__)

#define CM_DUMP(desc, ...) \
	do \
	{ \
		std::fprintf(stdout, _CM_LOG_STR_DESC desc _CM_LOG_SEPARATOR_CHAR, _CM_LOG_ARG, ##__VA_ARGS__); \
		std::fflush(stdout); \
	} \
	while (false)

#define CM_ABORT(desc, ...) \
	do \
	{ \
		std::fprintf(stderr, "[ABORT]" _CM_LOG_STR_DESC desc _CM_LOG_SEPARATOR_CHAR, _CM_LOG_ARG, ##__VA_ARGS__); \
		std::fflush(stderr); \
		std::abort(); \
	} \
	while (false)

#define CM_ASSERT(condition, desc, ...) \
	if (!(condition)) \
	{ \
		CM_ABORT("failed assertion '%s': " desc, #condition, ##__VA_ARGS__); \
	}

// clang-format on

#endif
