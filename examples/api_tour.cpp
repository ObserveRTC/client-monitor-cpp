/**
 * The snippets of docs/USAGE.md, compiled and run. If you change one, change the other: CI builds
 * this file, so a snippet that no longer matches the API breaks the build here first.
 */

#include <clientmonitor/ClientMonitor.hpp>
#include <clientmonitor/Logger.hpp>
#include <clientmonitor/schema/ClientEventTypes.hpp>

#include <iostream>
#include <memory>
#include <string>

using json = nlohmann::json;

/* Listener: what the application hears back. */

class AppListener : public observertc::clientmonitor::ClientMonitor::Listener
{
public:
	void OnSampleCreated(observertc::clientmonitor::ClientMonitor* /*monitor*/, const observertc::clientmonitor::ClientSample& sample) override
	{
		// Queue it for your network thread; do not send it from here.
		lastSample = sample.ToJson().dump();
	}

	void OnIssue(observertc::clientmonitor::ClientMonitor* /*monitor*/, const observertc::clientmonitor::ReportedClientIssue& issue) override
	{
		std::cout << "issue " << issue.type << (issue.resolvable ? " (raised, key " + issue.key + ")" : "")
		          << " " << issue.payload.dump() << std::endl;
	}

	void OnIssueResolved(observertc::clientmonitor::ClientMonitor* /*monitor*/, const observertc::clientmonitor::ResolvedClientIssue& issue) override
	{
		std::cout << "resolved " << issue.key << ": " << issue.comment.value_or("") << std::endl;
	}

	void OnClientEvent(observertc::clientmonitor::ClientMonitor* /*monitor*/, const observertc::clientmonitor::ClientEvent& event) override
	{
		std::cout << "event " << event.type << std::endl;
	}

	std::string lastSample;
};

int main()
{
	observertc::clientmonitor::Logger::SetDefaultHandler();
	observertc::clientmonitor::Logger::SetLogLevel(observertc::clientmonitor::Logger::LogLevel::LOG_WARN);

	/* Create a monitor. */

	observertc::clientmonitor::ClientMonitorConfig config;

	config.clientId             = "client-1";
	config.callId               = "call-1";
	config.collectingPeriodInMs = 0; // this tour drives Collect() itself; keep the 5000 default in an app
	config.samplingPeriodInMs   = 5000;

	AppListener listener;
	observertc::clientmonitor::ClientMonitor monitor(config, &listener);

	monitor.SetAttachments({ { "roomId", "room-42" }, { "userId", "user-7" } });

	// Merge one key in (or out again with null) without touching the others.
	monitor.UpdateAttachments(
	  {
	    { "senderSideCoupledCongestion", { { "enabled", true }, { "enforcedPolicy", "v1" } } }
  });
	monitor.UpdateAttachments(
	  {
	    { "senderSideCoupledCongestion", nullptr }
  });

	/* Client events. */

	// A custom event, buffered into the next sample.
	monitor.AddEvent("USER_RAISED_HAND", { { "userId", "user-7" } });

	// One of the library's own event types, with an explicit timestamp (epoch milliseconds).
	monitor.AddEvent(
	  observertc::clientmonitor::ClientEventTypes::MEDIA_TRACK_MUTED,
	  { { "trackId", "local-audio" }, { "reason", "user" } },
	  1700000000000);

	/* Meta data. */

	monitor.AddMetaData(
	  observertc::clientmonitor::ClientMetaTypes::OPERATION_SYSTEM, { { "name", "macOS" }, { "version", "15.1" } });

	// Every field given, for a payload that is not an object.
	observertc::clientmonitor::ClientMetaData sdp;

	sdp.type             = observertc::clientmonitor::ClientMetaTypes::LOCAL_SDP;
	sdp.payload          = "v=0\r\n...";
	sdp.peerConnectionId = "pc-1";
	monitor.AddMetaData(sdp);

	/* One-shot issues. */

	monitor.AddIssue("user-reported-bad-audio", { { "rating", 2 }, { "comment", "robotic voice" } });

	// Reported to listeners only, kept out of the sample.
	monitor.AddIssue("local-diagnostic", { { "detail", "x" } }, std::nullopt, /*includeInSample*/ false);

	/* Stateful issues: raise, update, resolve. */

	monitor.RaiseIssue("signalling-down", "signalling-unreachable", { { "attempts", 1 } });

	// Raising the same key again updates it in place (OnIssueUpdated), it does not duplicate it.
	monitor.RaiseIssue("signalling-down", "signalling-unreachable", { { "attempts", 3 } });

	if (monitor.IsIssueActive("signalling-down"))
	{
		for (const auto& issue : monitor.GetActiveIssuesByType("signalling-unreachable"))
		{
			std::cout << issue.key << " open since " << issue.raisedAt << ", " << issue.payload.dump() << std::endl;
		}
	}

	auto resolved = monitor.ResolveIssue("signalling-down", std::string("reconnected"), { { "reconnectInMs", 1800 } });

	if (!resolved || monitor.IsIssueActive("signalling-down"))
	{
		return 1;
	}

	/* Extension stats. */

	// Without an id: goes into the sample, nothing is kept.
	monitor.AddExtensionStats({ "render-stats", { { "canvasFps", 24 } }, std::nullopt });

	// With an id: also readable back until one collection after it stops being reported.
	monitor.AddExtensionStats({ "render-stats", { { "droppedFrames", 3 }, { "canvasFps", 24 } }, std::string("tile-42") });

	if (auto payload = monitor.GetExtensionStatsPayload("tile-42"))
	{
		std::cout << "tile-42 dropped " << (*payload)["droppedFrames"].get<int>() << " frames" << std::endl;
	}

	// A provider runs on every collection, so its value stays readable for the whole call.
	int cpuReading{ 40 };
	const auto providerHandle = monitor.AddExtensionStatsProvider(
	  [&cpuReading]() -> std::optional<observertc::clientmonitor::ClientMonitor::ExtensionStatsInput>
	  {
		  return observertc::clientmonitor::ClientMonitor::ExtensionStatsInput{
			  "system-metrics", { { "cpu", cpuReading } }, std::string("system")
		  };
	  });

	/* A peer connection fed by the application (a binding does this in a real client). */

	double timestamp{ 1000 };
	int64_t bytes{ 0 };

	auto collector = std::make_shared<observertc::clientmonitor::FunctionStatsCollector>(
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

	auto* peerConnection = monitor.AddPeerConnection(collector, "pc-1", { { "role", "recv" } });

	peerConnection->AddMediaTrack(
	  std::make_shared<observertc::clientmonitor::BasicMediaTrack>("remote-audio", "audio"), { { "participant", "alice" } });

	// Attachments on a data channel, by its SCTP stream id (dataChannelIdentifier), before or after
	// its stats appear. Any other stats object takes them once it exists:
	// monitor->UpdateAttachments({ ... }) on the monitor GetOutboundRtpMonitor(ssrc) etc. return.
	peerConnection->SetDataChannelAttachments(
	  1,
	  {
	    { "dataProducerId", "data-producer-1" }
  });

	/* Track context: what the stats cannot show. Declarable before the track appears. */

	observertc::clientmonitor::InboundTrackContext context;

	context.paused = true; // e.g. a mediasoup consumer created paused
	monitor.SetInboundTrackContext("remote-audio", context);

	context.paused = false; // and resumed later
	monitor.SetInboundTrackContext("remote-audio", context);

	/* Collect and read state back. */

	monitor.Collect();
	monitor.Collect();

	cpuReading = 55;
	monitor.Collect();

	std::cout << "receiving audio " << peerConnection->GetMetrics().receivingAudioBitrate << " bps" << std::endl;

	if (auto* track = monitor.GetInboundTrackMonitor("remote-audio"))
	{
		std::cout << "remote-audio dry: " << (track->GetVerdicts().dry.value_or(false) ? "yes" : "no") << std::endl;
	}

	std::cout << "system cpu " << monitor.GetExtensionStatsPayload("system").value()["cpu"] << std::endl;

	monitor.RemoveExtensionStatsProvider(providerHandle);

	/* Detectors: turn one off at runtime, or leave it out through the config. */

	peerConnection->GetDetectors().Disable("transport-delay-detector");
	peerConnection->GetDetectors().Enable("transport-delay-detector");

	/* Score: set it directly, or install a ScoreCalculator with SetScoreCalculator(). */

	monitor.SetScore(3.5, { { "audio-dry", 1.5 } });
	std::cout << "score " << monitor.GetScore() << std::endl;

	/* A sample on demand (normally created every samplingPeriodInMs). */

	if (auto sample = monitor.CreateSample())
	{
		std::cout << "sample with " << sample->peerConnections->size() << " peer connection(s)" << std::endl;
	}

	if (listener.lastSample.empty())
	{
		return 1;
	}

	/* Shut down: resolves open issues, adds CLIENT_LEFT and a last sample. */

	monitor.RaiseIssue("left-open", "demo-issue");
	monitor.Close();

	return 0;
}
