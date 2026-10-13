/**
 * The smallest useful integration: one peer connection whose stats come from a callable, a
 * listener that prints every sample, and collection driven by the application.
 *
 * In a real client the collector is libwebrtc::PeerConnectionStatsCollector or the
 * mediasoupclient TransportMonitor (see bindings/), and the timer is left on.
 */

#include <clientmonitor/ClientMonitor.hpp>
#include <clientmonitor/Logger.hpp>
#include <clientmonitor/version.hpp>

#include <iostream>

using json = nlohmann::json;

class PrintingListener : public clientmonitor::ClientMonitor::Listener
{
public:
	void OnSampleCreated(
	  clientmonitor::ClientMonitor* /*monitor*/, const clientmonitor::ClientSample& sample) override
	{
		// This is what goes to the observer.
		std::cout << "sample: " << sample.ToJson().dump() << std::endl;
	}

	void OnIssue(
	  clientmonitor::ClientMonitor* /*monitor*/, const clientmonitor::ReportedClientIssue& issue) override
	{
		std::cout << "issue: " << issue.type << " " << issue.payload.dump() << std::endl;
	}
};

int main()
{
	clientmonitor::Logger::SetLogLevel(clientmonitor::Logger::LogLevel::LOG_WARN);
	clientmonitor::Logger::SetDefaultHandler();

	std::cout << "client-monitor-cpp " << clientmonitor::Version << std::endl;

	clientmonitor::ClientMonitorConfig config;

	config.clientId             = "client-1";
	config.callId               = "call-1";
	config.collectingPeriodInMs = 0; // driven by hand below
	config.samplingPeriodInMs   = 1;

	PrintingListener listener;
	clientmonitor::ClientMonitor monitor(config, &listener);

	double timestamp{ 1000 };
	int64_t bytes{ 0 };

	auto collector = std::make_shared<clientmonitor::FunctionStatsCollector>(
	  [&]()
	  {
		  timestamp += 1000;
		  bytes += 4000;

		  return json::array({
		    { { "type", "inbound-rtp" },
         { "id", "IA1" },
         { "timestamp", timestamp },
         { "ssrc", 1234 },
         { "kind", "audio" },
         { "trackIdentifier", "remote-audio" },
         { "bytesReceived", bytes },
         { "packetsReceived", bytes / 100 } },
		  });
	  });

	auto* peerConnection = monitor.AddPeerConnection(collector, "pc-1");

	peerConnection->AddMediaTrack(
	  std::make_shared<clientmonitor::BasicMediaTrack>("remote-audio", "audio"));

	for (int i = 0; i < 3; ++i)
	{
		monitor.Collect();
	}

	std::cout << "receiving audio bitrate: " << peerConnection->GetMetrics().receivingAudioBitrate
	          << " bps" << std::endl;

	monitor.RaiseIssue(
	  "app-signalling",
	  "signalling-unreachable",
	  {
	    { "attempts", 3 }
  });
	monitor.Close();

	return 0;
}
