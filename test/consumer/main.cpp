#include <clientmonitor/ClientMonitor.hpp>
#include <clientmonitor/version.hpp>

#include <iostream>

int main()
{
	observertc::clientmonitor::ClientMonitorConfig config;

	config.collectingPeriodInMs = 0;

	observertc::clientmonitor::ClientMonitor monitor(config);
	auto sample = monitor.CreateSample();

	std::cout << "client-monitor-cpp " << observertc::clientmonitor::Version << ", schema "
	          << observertc::schemas::SchemaVersion << ": " << sample->ToJson().dump() << std::endl;

	return sample.has_value() ? 0 : 1;
}
