#include "clientmonitor/ClientMonitor.hpp"
#include "clientmonitor/StatsCollector.hpp"
#include "helpers.hpp"

#include <catch.hpp>

#include <set>

using json = nlohmann::json;
using namespace observertc::clientmonitor;

namespace
{
	/**
	 * A send transport after senders were replaced by new ones for tracks with the same id, as
	 * libwebrtc reports it: two media sources carry the track's identifier, and the closed senders
	 * are still reported with counters that no longer move (mid "0" with its last timestamp too). The closed sender with mid "0"
	 * references the old media source, the one with mid "2" the new one. The live sender (mid "1")
	 * sends two simulcast layers from the new media source.
	 */
	struct Report
	{
		double timestamp{ 1000 };
		int64_t liveBytes{ 0 };

		json Build() const
		{
			const double ts = this->timestamp;

			return json::array({
			  { { "type", "media-source" },
			    { "id", "MS1" },
			    { "timestamp", ts },
			    { "kind", "video" },
			    { "trackIdentifier", "local-video" } },
			  { { "type", "media-source" },
			    { "id", "MS2" },
			    { "timestamp", ts },
			    { "kind", "video" },
			    { "trackIdentifier", "local-video" } },
			  // libwebrtc repeats the last report of a sender whose track was removed, timestamp included.
			  { { "type", "outbound-rtp" },
			    { "id", "O1111" },
			    { "timestamp", 1000 },
			    { "ssrc", 1111 },
			    { "kind", "video" },
			    { "mid", "0" },
			    { "active", true },
			    { "mediaSourceId", "MS1" },
			    { "bytesSent", 5000 } },
			  { { "type", "outbound-rtp" },
			    { "id", "O4444" },
			    { "timestamp", ts },
			    { "ssrc", 4444 },
			    { "kind", "video" },
			    { "mid", "2" },
			    { "active", true },
			    { "mediaSourceId", "MS2" },
			    { "bytesSent", 0 } },
			  { { "type", "outbound-rtp" },
			    { "id", "O2222" },
			    { "timestamp", ts },
			    { "ssrc", 2222 },
			    { "kind", "video" },
			    { "mid", "1" },
			    { "rid", "r0" },
			    { "active", true },
			    { "mediaSourceId", "MS2" },
			    { "bytesSent", this->liveBytes } },
			  { { "type", "outbound-rtp" },
			    { "id", "O3333" },
			    { "timestamp", ts },
			    { "ssrc", 3333 },
			    { "kind", "video" },
			    { "mid", "1" },
			    { "rid", "r1" },
			    { "active", true },
			    { "mediaSourceId", "MS2" },
			    { "bytesSent", 2 * this->liveBytes } },
			});
		}
	};

	const json* Find(const json& stats, const std::string& id)
	{
		for (const auto& stat : stats)
		{
			if (stat["id"] == id)
			{
				return &stat;
			}
		}

		return nullptr;
	}

	struct Fixture
	{
		Fixture()
		  : monitor(helpers::ManualConfig(), &listener),
		    collector(std::make_shared<helpers::ScriptedStatsCollector>())
		{
			pc = monitor.AddPeerConnection(collector, "pc-1");
			monitor.AddStatsAdapter(std::make_unique<DetachedSenderStatsAdapter>());
			adapter = dynamic_cast<DetachedSenderStatsAdapter*>(monitor.GetStatsAdapter(DetachedSenderStatsAdapter::Name));
		}

		/** One second of stats time; the live sender sends 1000 bytes unless `liveSends` is false. */
		void Tick(bool liveSends = true)
		{
			if (liveSends)
			{
				report.liveBytes += 1000;
			}

			collector->next = report.Build();
			monitor.Collect();
			report.timestamp += 1000;
			clock.Advance(1000);
		}

		std::set<int64_t> BoundSsrcs(const std::string& trackId) const
		{
			std::set<int64_t> result;

			if (auto* track = pc->GetOutboundTrackMonitor(trackId))
			{
				for (auto* rtp : track->GetOutboundRtps())
				{
					result.insert(rtp->GetSsrc());
				}
			}

			return result;
		}

		helpers::FakeClock clock;
		helpers::RecordingListener listener;
		ClientMonitor monitor;
		std::shared_ptr<helpers::ScriptedStatsCollector> collector;
		PeerConnectionMonitor* pc{ nullptr };
		DetachedSenderStatsAdapter* adapter{ nullptr };
		Report report;
	};
} // namespace

TEST_CASE("DetachedSenderStatsAdapter", "[adapters]")
{
	DetachedSenderStatsAdapter adapter;
	auto stats      = Report{}.Build();
	const auto size = stats.size();

	adapter.SetSenderTrack("pc-1", "1", true);
	adapter.Adapt("pc-1", stats);

	SECTION("unlinks only the streams of senders without a track, and removes nothing")
	{
		REQUIRE(stats.size() == size);
		REQUIRE((*Find(stats, "O2222"))["mediaSourceId"] == "MS2");
		REQUIRE((*Find(stats, "O3333"))["mediaSourceId"] == "MS2");
		REQUIRE_FALSE(Find(stats, "O1111")->contains("mediaSourceId"));
		REQUIRE_FALSE(Find(stats, "O4444")->contains("mediaSourceId"));
		REQUIRE((*Find(stats, "O1111"))["bytesSent"] == 5000);
		REQUIRE((*Find(stats, "MS1"))["trackIdentifier"] == "local-video");
	}

	SECTION("another peer connection's report passes unchanged")
	{
		auto other = Report{}.Build();

		adapter.Adapt("pc-2", other);

		REQUIRE(other == Report{}.Build());
	}

	SECTION("a sender whose track was removed is unlinked too")
	{
		auto next = Report{}.Build();

		adapter.SetSenderTrack("pc-1", "1", false);
		adapter.Adapt("pc-1", next);

		for (const auto& stat : next)
		{
			if (stat["type"] == "outbound-rtp")
			{
				REQUIRE_FALSE(stat.contains("mediaSourceId"));
			}
		}
	}
}

TEST_CASE("LibWebRtcStatsAdapter brings microsecond timestamps to milliseconds", "[adapters]")
{
	LibWebRtcStatsAdapter adapter;
	json stats = json::array({
	  { { "type", "codec" }, { "id", "C1" }, { "timestamp", 1792044664814755.0 } },
	  { { "type", "codec" }, { "id", "C2" }, { "timestamp", 1792044664814.0 } },
	});

	adapter.Adapt("pc-1", stats);

	REQUIRE(stats[0]["timestamp"].get<double>() == Approx(1792044664814.755));
	REQUIRE(stats[1]["timestamp"].get<double>() == Approx(1792044664814.0));
}

TEST_CASE("Microsecond reports: rates and samples in milliseconds", "[adapters]")
{
	// The same stream reported once a second, in microseconds as libwebrtc writes them.
	helpers::FakeClock clock;
	helpers::RecordingListener listener;
	ClientMonitor monitor(helpers::ManualConfig(), &listener);
	auto collector = std::make_shared<helpers::ScriptedStatsCollector>();
	auto* pc       = monitor.AddPeerConnection(collector, "pc-1");
	double ts      = 1792044664000000.0;
	int64_t bytes  = 0;

	auto report = [&]()
	{
		return json::array({
		  { { "type", "media-source" }, { "id", "MS1" }, { "timestamp", ts }, { "kind", "video" }, { "trackIdentifier", "local-video" } },
		  { { "type", "outbound-rtp" }, { "id", "O1" }, { "timestamp", ts }, { "ssrc", 1 }, { "kind", "video" }, { "mediaSourceId", "MS1" }, { "bytesSent", bytes } },
		});
	};

	pc->AddMediaTrack(std::make_shared<BasicMediaTrack>("local-video", "video"));

	for (int i = 0; i < 3; ++i)
	{
		bytes += 125000; // 1 Mbps
		collector->next = report();
		monitor.Collect();
		ts += 1000000.0;
		clock.Advance(1000);
	}

	auto* rtp = pc->GetOutboundRtpMonitor(1);

	REQUIRE(rtp->GetDeltaTime().value() == Approx(1000.0));
	REQUIRE(rtp->GetMetrics().bitrate.value() == Approx(1000000.0));
	REQUIRE(pc->GetMetrics().deltaTime.value() == Approx(1000.0));

	const auto sample = monitor.CreateSample();

	REQUIRE(sample->peerConnections->front().outboundRtps->front().timestamp == Approx((ts - 1000000.0) / 1000.0));
}

TEST_CASE("Outbound tracks bind to the streams of the sender that carries them", "[monitors]")
{
	Fixture f;

	f.adapter->SetSenderTrack("pc-1", "1", true);
	f.Tick();
	f.pc->AddMediaTrack(
	  std::make_shared<BasicMediaTrack>("local-video", "video"),
	  {
	    { "producerId", "p-2" }
  });

	SECTION("every layer of the live sender, and the media source they reference")
	{
		f.Tick();

		REQUIRE(f.BoundSsrcs("local-video") == std::set<int64_t>{ 2222, 3333 });
		REQUIRE(f.pc->GetOutboundTrackMonitor("local-video")->GetMediaSource()->GetStats().id == "MS2");
	}

	SECTION("a sending track is not dry while the closed senders' counters stand still")
	{
		for (int i = 0; i < 10; ++i)
		{
			f.Tick();
		}

		REQUIRE(f.listener.dryOutbound == 0);
		REQUIRE_FALSE(f.monitor.IsIssueActive("dry-outbound-track-track-local-video"));
	}

	SECTION("the closed senders' streams and both media sources are still sampled")
	{
		f.Tick();

		const auto sample    = f.monitor.CreateSample();
		const auto& pcSample = sample->peerConnections->front();

		REQUIRE(pcSample.outboundRtps->size() == 4);
		REQUIRE(pcSample.mediaSources->size() == 2);
	}

	SECTION("a track whose sender has no track any more is bound to nothing and never dry")
	{
		f.adapter->SetSenderTrack("pc-1", "1", false);

		for (int i = 0; i < 10; ++i)
		{
			f.Tick(false);
		}

		REQUIRE(f.BoundSsrcs("local-video").empty());
		REQUIRE(f.listener.dryOutbound == 0);
	}

	SECTION("a bound sender that stops sending is dry, measured from when it stopped")
	{
		f.Tick();
		f.Tick();

		for (int i = 0; i < 8; ++i)
		{
			f.Tick(false);
		}

		REQUIRE(f.listener.dryOutbound == 1);
		REQUIRE(f.listener.issues.back().type == "dry-outbound-track");
		REQUIRE(f.listener.issues.back().payload["dryForInMs"].get<double>() <= 8000);
	}
}

TEST_CASE("A new track with the id of an ended one starts a new track monitor", "[monitors]")
{
	Fixture f;

	f.adapter->SetSenderTrack("pc-1", "0", true);
	f.pc->AddMediaTrack(
	  std::make_shared<BasicMediaTrack>("local-video", "video"),
	  {
	    { "producerId", "p-1" }
  });
	f.Tick();

	REQUIRE(f.BoundSsrcs("local-video") == std::set<int64_t>{ 1111 });

	// The first sender is closed; a new one is created with a track of the same id.
	f.adapter->SetSenderTrack("pc-1", "0", false);
	f.pc->OnMediaTrackEnded("local-video");
	f.adapter->SetSenderTrack("pc-1", "1", true);
	f.pc->AddMediaTrack(
	  std::make_shared<BasicMediaTrack>("local-video", "video"),
	  {
	    { "producerId", "p-2" }
  });

	for (int i = 0; i < 10; ++i)
	{
		f.Tick();
	}

	auto* track = f.pc->GetOutboundTrackMonitor("local-video");

	REQUIRE(track);
	REQUIRE(track->GetAttachments() == json({ { "producerId", "p-2" } }));
	REQUIRE(f.BoundSsrcs("local-video") == std::set<int64_t>{ 2222, 3333 });
	REQUIRE(f.listener.dryOutbound == 0);
}

TEST_CASE("A stream follows its sender to a new media source", "[monitors]")
{
	helpers::FakeClock clock;
	helpers::RecordingListener listener;
	ClientMonitor monitor(helpers::ManualConfig(), &listener);
	auto collector = std::make_shared<helpers::ScriptedStatsCollector>();
	auto* pc       = monitor.AddPeerConnection(collector, "pc-1");
	double ts      = 1000;

	auto report = [&](const std::string& mediaSourceId)
	{
		return json::array({
		  { { "type", "media-source" }, { "id", "MS-A" }, { "timestamp", ts }, { "kind", "video" }, { "trackIdentifier", "track-a" } },
		  { { "type", "media-source" }, { "id", "MS-B" }, { "timestamp", ts }, { "kind", "video" }, { "trackIdentifier", "track-b" } },
		  { { "type", "outbound-rtp" }, { "id", "O1" }, { "timestamp", ts }, { "ssrc", 1 }, { "kind", "video" }, { "mid", "0" }, { "mediaSourceId", mediaSourceId }, { "bytesSent", ts } },
		});
	};

	pc->AddMediaTrack(std::make_shared<BasicMediaTrack>("track-a", "video"));
	pc->AddMediaTrack(std::make_shared<BasicMediaTrack>("track-b", "video"));
	collector->next = report("MS-A");
	monitor.Collect();
	ts += 1000;

	REQUIRE(pc->GetOutboundTrackMonitor("track-a")->GetOutboundRtps().size() == 1);
	REQUIRE(pc->GetOutboundTrackMonitor("track-b")->GetOutboundRtps().empty());

	// replaceTrack(): the sender now carries track-b.
	collector->next = report("MS-B");
	monitor.Collect();

	REQUIRE(pc->GetOutboundTrackMonitor("track-a")->GetOutboundRtps().empty());
	REQUIRE(pc->GetOutboundTrackMonitor("track-b")->GetOutboundRtps().size() == 1);
}

TEST_CASE("The client monitor reports the raw stats, then adapts them for the monitors", "[adapters]")
{
	struct RawListener : ClientMonitor::Listener
	{
		void OnRawStats(ClientMonitor*, const std::string& peerConnectionId, const json& rawStats) override
		{
			raw.emplace_back(peerConnectionId, rawStats);
		}
		void OnStatsCollected(ClientMonitor*, const StatsCollectedEvent& event) override
		{
			adapted = event.collectedStats;
		}

		std::vector<std::pair<std::string, json>> raw;
		std::vector<std::pair<std::string, json>> adapted;
	};

	helpers::FakeClock clock;
	RawListener listener;
	ClientMonitor monitor(helpers::ManualConfig(), &listener);
	auto collector = std::make_shared<helpers::ScriptedStatsCollector>();
	auto* pc       = monitor.AddPeerConnection(collector, "pc-1");

	monitor.AddStatsAdapter(std::make_unique<DetachedSenderStatsAdapter>());
	{
		std::lock_guard<std::recursive_mutex> lock(monitor.GetMutex());

		dynamic_cast<DetachedSenderStatsAdapter*>(monitor.GetStatsAdapter(DetachedSenderStatsAdapter::Name))
		  ->SetSenderTrack("pc-1", "1", true);
	}

	auto report = Report{}.Build();

	report[0]["timestamp"] = 1792044664814755.0; // microseconds, as libwebrtc writes them

	collector->next = report;
	monitor.Collect();

	REQUIRE(listener.raw.size() == 1);
	REQUIRE(listener.raw[0].first == "pc-1");
	REQUIRE(listener.raw[0].second == report);
	REQUIRE(listener.adapted.size() == 1);
	REQUIRE((*Find(listener.adapted[0].second, "MS1"))["timestamp"].get<double>() == Approx(1792044664814.755));
	REQUIRE_FALSE(Find(listener.adapted[0].second, "O1111")->contains("mediaSourceId"));

	SECTION("a report the application accepts goes through the same adapters")
	{
		listener.raw.clear();
		pc->Accept(report);

		REQUIRE(listener.raw.size() == 1);
		REQUIRE(pc->GetOutboundRtpMonitor(1111)->GetStats().mediaSourceId == std::nullopt);
	}
}
