#include "helpers.hpp"
#include "clientmonitor/utils/Utils.hpp"

using json = nlohmann::json;

namespace helpers
{
	FakeClock::FakeClock(int64_t start) : now(std::make_shared<int64_t>(start))
	{
		auto now = this->now;

		clientmonitor::utils::SetClock(
		  [now]()
		  {
			  return *now;
		  });
	}

	FakeClock::~FakeClock()
	{
		clientmonitor::utils::SetClock(nullptr);
	}

	int64_t FakeClock::Now() const
	{
		return *this->now;
	}

	void FakeClock::Advance(int64_t ms)
	{
		*this->now += ms;
	}

	json ScriptedStatsCollector::GetStats()
	{
		++this->calls;

		return this->next;
	}

	clientmonitor::ClientMonitorConfig ManualConfig()
	{
		clientmonitor::ClientMonitorConfig config;

		config.collectingPeriodInMs = 0;
		config.samplingPeriodInMs   = 1000;
		config.clientId             = "client-1";
		config.callId               = "call-1";

		return config;
	}

	json BuildReport(const Counters& c)
	{
		const double ts = c.timestamp;

		return json::array({
		  { { "type", "transport" },
       { "id", "T01" },
       { "timestamp", ts },
       { "iceState", c.iceState },
       { "dtlsState", "connected" },
       { "iceRole", "controlling" },
       { "iceLocalUsernameFragment", c.ufrag },
       { "selectedCandidatePairId", "CP1" },
       { "bytesSent", c.outboundBytes },
       { "bytesReceived", c.inboundBytes },
       { "packetsSent", c.outboundPackets },
       { "packetsReceived", c.inboundPackets } },
		  { { "type", "candidate-pair" },
       { "id", "CP1" },
       { "timestamp", ts },
       { "transportId", "T01" },
       { "localCandidateId", "L1" },
       { "remoteCandidateId", "R1" },
       { "state", "succeeded" },
       { "nominated", true },
       { "currentRoundTripTime", c.rtt },
       { "availableOutgoingBitrate", 1500000 } },
		  { { "type", "local-candidate" },
       { "id", "L1" },
       { "timestamp", ts },
       { "transportId", "T01" },
       { "ip", "10.0.0.1" },
       { "port", 50000 },
       { "protocol", "udp" },
       { "candidateType", "host" } },
		  { { "type", "remote-candidate" },
       { "id", "R1" },
       { "timestamp", ts },
       { "transportId", "T01" },
       { "address", "203.0.113.7" },
       { "port", 3478 },
       { "protocol", "udp" },
       { "candidateType", "srflx" } },
		  { { "type", "codec" },
       { "id", "CA" },
       { "timestamp", ts },
       { "mimeType", "audio/opus" },
       { "payloadType", 111 },
       { "clockRate", 48000 } },
		  { { "type", "codec" },
       { "id", "CV" },
       { "timestamp", ts },
       { "mimeType", "video/VP8" },
       { "payloadType", 96 },
       { "clockRate", 90000 } },
		  { { "type", "inbound-rtp" },
       { "id", "IA1111" },
       { "timestamp", ts },
       { "ssrc", 1111 },
       { "mediaType", "audio" },
       { "trackIdentifier", "remote-audio" },
       { "codecId", "CA" },
       { "bytesReceived", c.inboundBytes },
       { "packetsReceived", c.inboundPackets },
       { "packetsLost", c.inboundLost },
       { "jitter", 0.01 } },
		  { { "type", "media-source" },
       { "id", "MS1" },
       { "timestamp", ts },
       { "kind", "video" },
       { "trackIdentifier", "local-video" },
       { "frames", c.mediaSourceFrames } },
		  { { "type", "outbound-rtp" },
       { "id", "OV2222" },
       { "timestamp", ts },
       { "ssrc", 2222 },
       { "kind", "video" },
       { "codecId", "CV" },
       { "bytesSent", std::to_string(c.outboundBytes) },
       { "packetsSent", c.outboundPackets },
       { "framesEncoded", c.framesEncoded } },
		  { { "type", "remote-inbound-rtp" },
       { "id", "RIV2222" },
       { "timestamp", ts },
       { "ssrc", 2222 },
       { "kind", "video" },
       { "packetsLost", 0 },
       { "roundTripTime", c.rtt },
       { "totalRoundTripTime", c.rtcpTotalRtt },
       { "roundTripTimeMeasurements", c.rtcpRttMeasurements } },
		  { { "type", "certificate" },
       { "id", "CERT1" },
       { "timestamp", ts },
       { "fingerprint", "AA:BB" },
       { "fingerprintAlgorithm", "sha-256" } },
		});
	}

	/* RecordingListener */

	void RecordingListener::OnSampleCreated(
	  clientmonitor::ClientMonitor*, const clientmonitor::ClientSample& sample)
	{
		this->samples.push_back(sample);
	}

	void RecordingListener::OnIssue(
	  clientmonitor::ClientMonitor*, const clientmonitor::ReportedClientIssue& issue)
	{
		this->issues.push_back(issue);
	}

	void RecordingListener::OnIssueUpdated(
	  clientmonitor::ClientMonitor*, const clientmonitor::RaisedClientIssue& issue)
	{
		this->updatedIssues.push_back(issue);
	}

	void RecordingListener::OnIssueResolved(
	  clientmonitor::ClientMonitor*, const clientmonitor::ResolvedClientIssue& issue)
	{
		this->resolvedIssues.push_back(issue);
	}

	void RecordingListener::OnClientEvent(
	  clientmonitor::ClientMonitor*, const clientmonitor::ClientEvent& event)
	{
		this->events.push_back(event);
	}

	void RecordingListener::OnClose(clientmonitor::ClientMonitor*)
	{
		++this->closed;
	}

	void RecordingListener::OnStatsCollectionGap(
	  clientmonitor::ClientMonitor*, const StatsCollectionGapEvent& event)
	{
		this->gaps.push_back(event);
	}

	void RecordingListener::OnDryInboundTrack(
	  clientmonitor::ClientMonitor*, clientmonitor::InboundTrackMonitor*)
	{
		++this->dryInbound;
	}

	void RecordingListener::OnDryOutboundTrack(
	  clientmonitor::ClientMonitor*, clientmonitor::OutboundTrackMonitor*)
	{
		++this->dryOutbound;
	}

	void RecordingListener::OnTransportDelayDegraded(
	  clientmonitor::ClientMonitor*, clientmonitor::PeerConnectionMonitor*, double)
	{
		++this->transportDelay;
	}

	void RecordingListener::OnNewPeerConnectionMonitor(
	  clientmonitor::ClientMonitor*, clientmonitor::PeerConnectionMonitor*)
	{
		++this->newPeerConnections;
	}

	void RecordingListener::OnNewInboundTrackMonitor(
	  clientmonitor::ClientMonitor*, clientmonitor::InboundTrackMonitor*)
	{
		++this->newInboundTracks;
	}

	void RecordingListener::OnNewOutboundTrackMonitor(
	  clientmonitor::ClientMonitor*, clientmonitor::OutboundTrackMonitor*)
	{
		++this->newOutboundTracks;
	}
} // namespace helpers
