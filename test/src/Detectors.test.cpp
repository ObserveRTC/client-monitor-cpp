#include "clientmonitor/ClientMonitor.hpp"
#include "clientmonitor/detectors/DryInboundTrackDetector.hpp"
#include "clientmonitor/detectors/TransportDelayDetector.hpp"
#include "helpers.hpp"
#include "clientmonitor/schema/ClientEventTypes.hpp"

#include <catch.hpp>

using json = nlohmann::json;
using namespace observertc::clientmonitor;

namespace
{
	struct Fixture
	{
		Fixture(ClientMonitorConfig config = helpers::ManualConfig())
		  : monitor(config, &listener), collector(std::make_shared<helpers::ScriptedStatsCollector>())
		{
			pc = monitor.AddPeerConnection(collector, "pc-1");
		}

		/** Advances one second of stats time and collects. */
		void Tick(const std::function<void(helpers::Counters&)>& change = nullptr)
		{
			if (change)
			{
				change(c);
			}

			collector->next = helpers::BuildReport(c);
			monitor.Collect();
			c.timestamp += 1000;
			clock.Advance(1000);
		}

		helpers::FakeClock clock;
		helpers::RecordingListener listener;
		ClientMonitor monitor;
		std::shared_ptr<helpers::ScriptedStatsCollector> collector;
		PeerConnectionMonitor* pc{ nullptr };
		helpers::Counters c;
	};
} // namespace

TEST_CASE("DryInboundTrackDetector", "[detectors]")
{
	Fixture f;

	f.pc->AddMediaTrack(std::make_shared<BasicMediaTrack>("remote-audio", "audio"));

	SECTION("raises after receiving nothing for thresholdInMs and resolves when bytes flow")
	{
		// Bytes flow, then stop.
		f.Tick(
		  [](helpers::Counters& c)
		  {
			  c.inboundBytes = 1000;
		  });
		f.Tick(
		  [](helpers::Counters& c)
		  {
			  c.inboundBytes = 2000;
		  });

		for (int i = 0; i < 6; ++i)
		{
			f.Tick();
		}

		auto* track = f.pc->GetInboundTrackMonitor("remote-audio");

		REQUIRE(f.listener.dryInbound == 1);
		REQUIRE(track->GetVerdicts().dry == true);
		REQUIRE(f.monitor.IsIssueActive("dry-inbound-track-track-remote-audio"));

		f.Tick(
		  [](helpers::Counters& c)
		  {
			  c.inboundBytes = 3000;
		  });

		REQUIRE_FALSE(f.monitor.IsIssueActive("dry-inbound-track-track-remote-audio"));
		REQUIRE(f.listener.resolvedIssues.back().comment == std::string("dry inbound track recovered"));
		REQUIRE(f.listener.resolvedIssues.back().payload.contains("durationInMs"));
	}

	SECTION("a paused track is never dry")
	{
		InboundTrackContext context;

		context.paused = true;
		f.monitor.SetInboundTrackContext("remote-audio", context);

		f.Tick(
		  [](helpers::Counters& c)
		  {
			  c.inboundBytes = 1000;
		  });

		for (int i = 0; i < 8; ++i)
		{
			f.Tick();
		}

		REQUIRE(f.listener.dryInbound == 0);
	}
}

TEST_CASE("DryOutboundTrackDetector", "[detectors]")
{
	Fixture f;

	f.Tick();
	f.pc->AddMediaTrack(std::make_shared<BasicMediaTrack>("local-video", "video"));

	f.Tick(
	  [](helpers::Counters& c)
	  {
		  c.outboundBytes = 1000;
	  });

	for (int i = 0; i < 7; ++i)
	{
		f.Tick();
	}

	REQUIRE(f.listener.dryOutbound == 1);
	REQUIRE(f.monitor.IsIssueActive("dry-outbound-track-track-local-video"));
}

TEST_CASE("TransportDelayDetector", "[detectors]")
{
	Fixture f;

	SECTION("raises on a sustained high RTCP round trip and resolves when it recovers")
	{
		for (int i = 0; i < 4; ++i)
		{
			f.Tick(
			  [](helpers::Counters& c)
			  {
				  c.rtcpTotalRtt += 0.4;
				  c.rtcpRttMeasurements += 1;
			  });
		}

		REQUIRE(f.listener.transportDelay == 1);
		REQUIRE(f.monitor.IsIssueActive("transport-delay-degraded-pc-pc-1"));

		const auto& raised = f.listener.issues.back();

		REQUIRE(raised.type == TransportDelayDetector::IssueType);
		REQUIRE(raised.payload["rttSource"] == "rtcp");
		REQUIRE(raised.payload["rttInMs"].get<double>() == Approx(400));

		for (int i = 0; i < 6; ++i)
		{
			f.Tick(
			  [](helpers::Counters& c)
			  {
				  c.rtcpTotalRtt += 0.05;
				  c.rtcpRttMeasurements += 1;
			  });
		}

		REQUIRE_FALSE(f.monitor.IsIssueActive("transport-delay-degraded-pc-pc-1"));
		REQUIRE(f.listener.resolvedIssues.back().comment == std::string("round trip recovered"));
	}

	SECTION("can be disabled entirely through the config")
	{
		auto config                   = helpers::ManualConfig();
		config.transportDelayDetector = std::nullopt;
		Fixture disabled(config);

		REQUIRE_FALSE(disabled.pc->GetDetectors().Has("transport-delay-detector"));
	}
}

TEST_CASE("IceDisconnectedDetector", "[detectors]")
{
	Fixture f;

	f.Tick();

	for (int i = 0; i < 7; ++i)
	{
		f.Tick(
		  [](helpers::Counters& c)
		  {
			  c.iceState = "disconnected";
		  });
	}

	const std::string key = "ice-disconnected-pc-pc-1-transport-T01";

	REQUIRE(f.monitor.IsIssueActive(key));
	REQUIRE(f.listener.issues.back().payload["iceGeneration"] == 0);

	SECTION("resolves on an ICE restart")
	{
		f.Tick(
		  [](helpers::Counters& c)
		  {
			  c.ufrag = "ufrag2";
		  });

		REQUIRE_FALSE(f.monitor.IsIssueActive(key));
		REQUIRE(f.listener.resolvedIssues.back().comment == std::string("ice restarted"));
	}

	SECTION("resolves when ICE reconnects")
	{
		f.Tick(
		  [](helpers::Counters& c)
		  {
			  c.iceState = "connected";
		  });

		REQUIRE_FALSE(f.monitor.IsIssueActive(key));
	}
}

TEST_CASE("StatsGapDetector", "[detectors]")
{
	auto config = helpers::ManualConfig();
	helpers::FakeClock clock;
	helpers::RecordingListener listener;

	// Collection driven by hand every 5s, with a stall simulated by the clock.
	config.statsGapDetector->expectedPeriodInMs = 5000;

	ClientMonitor monitor(config, &listener);

	monitor.Collect();
	clock.Advance(5000);
	monitor.Collect();

	REQUIRE(listener.gaps.empty());

	clock.Advance(30000);
	monitor.Collect();

	REQUIRE(listener.gaps.size() == 1);
	REQUIRE(listener.gaps[0].gapInMs == 25000);

	bool found = false;

	for (const auto& sample : listener.samples)
	{
		for (const auto& event : *sample.clientEvents)
		{
			found = found || event.type == ClientEventTypes::STATS_COLLECTION_GAP;
		}
	}

	REQUIRE(found);
}

TEST_CASE("Detectors", "[detectors]")
{
	Fixture f;
	auto& detectors = f.pc->GetDetectors();

	REQUIRE(detectors.Has("transport-delay-detector"));
	REQUIRE(detectors.Disable("transport-delay-detector"));
	REQUIRE_FALSE(detectors.IsEnabled("transport-delay-detector"));
	REQUIRE(detectors.Remove("transport-delay-detector"));
	REQUIRE_FALSE(detectors.Has("transport-delay-detector"));
}
