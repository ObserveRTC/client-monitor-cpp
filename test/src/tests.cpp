#define CATCH_CONFIG_RUNNER

#include "clientmonitor/Logger.hpp"

#include <catch.hpp>

int main(int argc, char* argv[])
{
	observertc::clientmonitor::Logger::SetLogLevel(observertc::clientmonitor::Logger::LogLevel::LOG_ERROR);
	observertc::clientmonitor::Logger::SetDefaultHandler();

	return Catch::Session().run(argc, argv);
}
