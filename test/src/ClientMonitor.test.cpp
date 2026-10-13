#include "clientmonitor/ClientMonitor.hpp"
#include "clientmonitor/utils/Utils.hpp"
#include "helpers.hpp"
#include "clientmonitor/schema/ClientEventTypes.hpp"

#include <catch.hpp>

#include <atomic>
#include <chrono>
#include <thread>

using json = nlohmann::json;
using namespace observertc::clientmonitor;

TEST_CASE("ClientMonitor", "[ClientMonitor]")
{
	helpers::FakeClock clock;
	helpers::RecordingListener listener;

	SECTION("reports CLIENT_JOINED on creation and buffers it into the first sample")
	{
		ClientMonitor monitor(helpers::ManualConfig(), &listener);

		REQUIRE(listener.events.size() == 1);
		REQUIRE(listener.events[0].type == ClientEventTypes::CLIENT_JOINED);

		monitor.Collect();

		REQUIRE(listener.samples.size() == 1);

		const auto& sample = listener.samples[0];

		REQUIRE(sample.clientId == std::string("client-1"));
		REQUIRE(sample.callId == std::string("call-1"));
		REQUIRE(sample.clientEvents->size() == 1);
		REQUIRE(sample.score == 5.0);

		// Drained by the sample.
		monitor.Collect();

		REQUIRE(listener.samples[1].clientEvents->empty());
	}

	SECTION("a sample serialises to the ObserveRTC schema")
	{
		ClientMonitor monitor(helpers::ManualConfig(), &listener);
		auto collector = std::make_shared<helpers::ScriptedStatsCollector>();
		helpers::Counters c;

		monitor.AddPeerConnection(
		  collector,
		  "pc-1",
		  {
		    { "room", "r1" }
    });
		collector->next = helpers::BuildReport(c);
		monitor.Collect();

		const auto j = listener.samples.back().ToJson();

		REQUIRE(j["peerConnections"][0]["peerConnectionId"] == "pc-1");
		REQUIRE(j["peerConnections"][0]["attachments"]["room"] == "r1");
		REQUIRE(j["peerConnections"][0]["inboundRtps"][0]["ssrc"] == 1111);
		REQUIRE(j["peerConnections"][0]["outboundRtps"][0]["bytesSent"] == 0);
	}

	SECTION("samples every samplingPeriod / collectingPeriod collections")
	{
		ClientMonitor monitor(helpers::ManualConfig(), &listener);

		// A timer far too slow to fire during the test; only the sampling tick matters.
		monitor.SetCollectingPeriod(100000);
		monitor.SetSamplingPeriod(300000);

		monitor.Collect();
		monitor.Collect();

		REQUIRE(listener.samples.empty());

		monitor.Collect();

		REQUIRE(listener.samples.size() == 1);

		monitor.SetCollectingPeriod(0);
	}

	SECTION("issues: raise, update, resolve, and the -resolved entry in the sample")
	{
		ClientMonitor monitor(helpers::ManualConfig(), &listener);

		monitor.RaiseIssue(
		  "signalling-down",
		  "signalling-unreachable",
		  {
		    { "attempts", 3 }
    });
		monitor.RaiseIssue(
		  "signalling-down",
		  "signalling-unreachable",
		  {
		    { "attempts", 4 }
    });

		REQUIRE(monitor.IsIssueActive("signalling-down"));
		REQUIRE(listener.issues.size() == 1);
		REQUIRE(listener.issues[0].resolvable);
		REQUIRE(listener.updatedIssues.size() == 1);
		REQUIRE(monitor.GetActiveIssuesByType("signalling-unreachable")[0].payload["attempts"] == 4);

		clock.Advance(500);

		auto resolved = monitor.ResolveIssue("signalling-down", std::string("reconnected"));

		REQUIRE(resolved.has_value());
		REQUIRE_FALSE(monitor.IsIssueActive("signalling-down"));

		monitor.AddIssue(
		  "user-reported-bad-audio",
		  {
		    { "rating", 2 }
    });

		REQUIRE_FALSE(listener.issues.back().resolvable);

		const auto sample = monitor.CreateSample();

		REQUIRE(sample->clientIssues->size() == 3);
		REQUIRE(sample->clientIssues->at(0).key == std::string("signalling-down"));
		REQUIRE(sample->clientIssues->at(1).type == "signalling-unreachable-resolved");
		REQUIRE(sample->clientIssues->at(1).payload["comment"] == "reconnected");
		REQUIRE(sample->clientIssues->at(1).payload["attempts"] == 4);
		REQUIRE_FALSE(sample->clientIssues->at(2).key.has_value());
	}

	SECTION("close resolves open issues, adds CLIENT_LEFT and a last sample")
	{
		ClientMonitor monitor(helpers::ManualConfig(), &listener);

		monitor.RaiseIssue("k", "t");
		monitor.Close();
		monitor.Close();

		REQUIRE(listener.resolvedIssues.size() == 1);
		REQUIRE(listener.closed == 1);
		REQUIRE(listener.samples.size() == 1);
		REQUIRE(listener.samples[0].clientEvents->back().type == ClientEventTypes::CLIENT_LEFT);
		REQUIRE(monitor.IsClosed());
		REQUIRE_FALSE(monitor.CreateSample().has_value());
	}

	SECTION("extension stats: readable back by id for one collection after the last report")
	{
		ClientMonitor monitor(helpers::ManualConfig(), &listener);

		monitor.AddExtensionStats({ "render-stats", { { "fps", 24 } }, std::string("tile-42") });

		REQUIRE(monitor.GetExtensionStatsPayload("tile-42").value()["fps"] == 24);

		monitor.Collect();

		REQUIRE(monitor.GetExtensionStatsPayload("tile-42").has_value());
		REQUIRE(listener.samples[0].extensionStats->size() == 1);

		monitor.Collect();

		REQUIRE_FALSE(monitor.GetExtensionStatsPayload("tile-42").has_value());
	}

	SECTION("extension stats providers run on every collection")
	{
		ClientMonitor monitor(helpers::ManualConfig(), &listener);
		int counter{ 0 };
		const auto handle = monitor.AddExtensionStatsProvider(
		  [&counter]()
		  {
			  return ClientMonitor::ExtensionStatsInput{ "system",
				                                           { { "n", ++counter } },
				                                           std::string("system") };
		  });

		monitor.Collect();
		monitor.Collect();

		REQUIRE(monitor.GetExtensionStatsPayload("system").value()["n"] == 2);
		REQUIRE(monitor.RemoveExtensionStatsProvider(handle));
	}

	SECTION("buffers samples until the first listener when asked to")
	{
		auto config                               = helpers::ManualConfig();
		config.bufferClientSamplesUntilSubscriber = true;
		ClientMonitor monitor(config);

		monitor.Collect();
		monitor.Collect();
		monitor.AddListener(&listener);

		REQUIRE(listener.samples.size() == 2);

		monitor.Collect();

		REQUIRE(listener.samples.size() == 3);
	}

	SECTION("events are dropped when nothing samples or buffers")
	{
		auto config               = helpers::ManualConfig();
		config.samplingPeriodInMs = 0;
		ClientMonitor monitor(config, &listener);

		monitor.AddEvent("CUSTOM");

		REQUIRE(listener.events.empty());
		REQUIRE_FALSE(monitor.CreateSample()->clientEvents->size());
	}

	SECTION("the timer collects on its own thread and Close() stops it")
	{
		observertc::clientmonitor::utils::SetClock(nullptr);

		struct CountingListener : public ClientMonitor::Listener
		{
			void OnStatsCollected(ClientMonitor*, const StatsCollectedEvent&) override
			{
				++collections;
			}

			std::atomic<int> collections{ 0 };
		} counting;

		auto config                 = helpers::ManualConfig();
		config.collectingPeriodInMs = 20;
		config.samplingPeriodInMs   = 40;
		ClientMonitor monitor(config, &counting);

		std::this_thread::sleep_for(std::chrono::milliseconds(150));
		monitor.Close();

		const int seen = counting.collections.load();

		REQUIRE(seen >= 2);

		std::this_thread::sleep_for(std::chrono::milliseconds(60));

		REQUIRE(counting.collections.load() == seen);
	}

	SECTION("Close() from inside a listener on the timer thread does not deadlock")
	{
		observertc::clientmonitor::utils::SetClock(nullptr);

		struct ClosingListener : public ClientMonitor::Listener
		{
			void OnStatsCollected(ClientMonitor* monitor, const StatsCollectedEvent&) override
			{
				monitor->Close();
				done = true;
			}

			std::atomic<bool> done{ false };
		} closing;

		auto config                 = helpers::ManualConfig();
		config.collectingPeriodInMs = 10;
		ClientMonitor monitor(config, &closing);

		for (int i = 0; i < 100 && !closing.done; ++i)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}

		REQUIRE(closing.done);
		REQUIRE(monitor.IsClosed());
	}
}
