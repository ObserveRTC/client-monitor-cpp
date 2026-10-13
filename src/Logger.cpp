#define CM_CLASS "Logger"

#include "clientmonitor/Logger.hpp"

#include <iostream>

namespace observertc::clientmonitor
{
	/* Class variables. */

	Logger::LogLevel Logger::logLevel{ Logger::LogLevel::LOG_WARN };
	Logger::LogHandlerInterface* Logger::handler{ nullptr };
	char Logger::buffer[Logger::BufferSize];
	std::mutex Logger::mutex;

	/* Class methods. */

	void Logger::SetLogLevel(Logger::LogLevel level)
	{
		Logger::logLevel = level;
	}

	void Logger::SetHandler(LogHandlerInterface* handler)
	{
		Logger::handler = handler;
	}

	void Logger::SetDefaultHandler()
	{
		static DefaultLogHandler defaultHandler;

		Logger::handler = &defaultHandler;
	}

	/* DefaultLogHandler */

	void Logger::DefaultLogHandler::OnLog(LogLevel /*level*/, char* payload, size_t /*len*/)
	{
		std::cout << payload << std::endl;
	}
} // namespace observertc::clientmonitor
