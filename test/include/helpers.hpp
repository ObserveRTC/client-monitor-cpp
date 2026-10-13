#ifndef CM_TEST_HELPERS_HPP
#define CM_TEST_HELPERS_HPP

#include "clientmonitor/ClientMonitor.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace helpers
{
	/** Drives utils::NowMs() for the lifetime of the object. */
	class FakeClock
	{
	public:
		explicit FakeClock(int64_t start = 1700000000000);
		~FakeClock();

		int64_t Now() const;
		void Advance(int64_t ms);

	private:
		std::shared_ptr<int64_t> now;
	};

	/** A StatsCollector whose next report the test sets. */
	class ScriptedStatsCollector : public clientmonitor::StatsCollector
	{
	public:
		nlohmann::json GetStats() override;

		nlohmann::json next = nlohmann::json::array();
		size_t calls{ 0 };
	};

	/** Config for deterministic tests: no timer, every Collect() samples. */
	clientmonitor::ClientMonitorConfig ManualConfig();

	/** A media-flowing report: one audio in, one video out, one transport, one selected pair. */
	struct Counters
	{
		double timestamp{ 1000 };
		int64_t inboundBytes{ 0 };
		int64_t inboundPackets{ 0 };
		int64_t inboundLost{ 0 };
		int64_t outboundBytes{ 0 };
		int64_t outboundPackets{ 0 };
		int64_t framesEncoded{ 0 };
		int64_t mediaSourceFrames{ 0 };
		double rtcpTotalRtt{ 0 };
		int64_t rtcpRttMeasurements{ 0 };
		double rtt{ 0.05 };
		std::string iceState{ "connected" };
		std::string ufrag{ "ufrag1" };
	};

	nlohmann::json BuildReport(const Counters& c);

	/** Records every listener call it cares about. */
	class RecordingListener : public clientmonitor::ClientMonitor::Listener
	{
	public:
		void OnSampleCreated(clientmonitor::ClientMonitor*, const clientmonitor::ClientSample& sample) override;
		void OnIssue(clientmonitor::ClientMonitor*, const clientmonitor::ReportedClientIssue& issue) override;
		void OnIssueUpdated(
		  clientmonitor::ClientMonitor*, const clientmonitor::RaisedClientIssue& issue) override;
		void OnIssueResolved(
		  clientmonitor::ClientMonitor*, const clientmonitor::ResolvedClientIssue& issue) override;
		void OnClientEvent(clientmonitor::ClientMonitor*, const clientmonitor::ClientEvent& event) override;
		void OnClose(clientmonitor::ClientMonitor*) override;
		void OnStatsCollectionGap(clientmonitor::ClientMonitor*, const StatsCollectionGapEvent& event) override;
		void OnDryInboundTrack(clientmonitor::ClientMonitor*, clientmonitor::InboundTrackMonitor*) override;
		void OnDryOutboundTrack(clientmonitor::ClientMonitor*, clientmonitor::OutboundTrackMonitor*) override;
		void OnTransportDelayDegraded(
		  clientmonitor::ClientMonitor*, clientmonitor::PeerConnectionMonitor*, double) override;
		void OnNewPeerConnectionMonitor(
		  clientmonitor::ClientMonitor*, clientmonitor::PeerConnectionMonitor*) override;
		void OnNewInboundTrackMonitor(
		  clientmonitor::ClientMonitor*, clientmonitor::InboundTrackMonitor*) override;
		void OnNewOutboundTrackMonitor(
		  clientmonitor::ClientMonitor*, clientmonitor::OutboundTrackMonitor*) override;

		std::vector<clientmonitor::ClientSample> samples;
		std::vector<clientmonitor::ReportedClientIssue> issues;
		std::vector<clientmonitor::RaisedClientIssue> updatedIssues;
		std::vector<clientmonitor::ResolvedClientIssue> resolvedIssues;
		std::vector<clientmonitor::ClientEvent> events;
		std::vector<StatsCollectionGapEvent> gaps;
		size_t closed{ 0 };
		size_t dryInbound{ 0 };
		size_t dryOutbound{ 0 };
		size_t transportDelay{ 0 };
		size_t newPeerConnections{ 0 };
		size_t newInboundTracks{ 0 };
		size_t newOutboundTracks{ 0 };
	};
} // namespace helpers

#endif
