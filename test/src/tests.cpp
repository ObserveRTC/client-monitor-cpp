#define CATCH_CONFIG_RUNNER

#include "clientmonitor/Logger.hpp"

#include <catch.hpp>

int main(int argc, char* argv[])
{
	clientmonitor::Logger::SetLogLevel(clientmonitor::Logger::LogLevel::LOG_ERROR);
	clientmonitor::Logger::SetDefaultHandler();

	return Catch::Session().run(argc, argv);
}
