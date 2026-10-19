#include "clientmonitor/ClientMonitor.hpp"
#include "clientmonitor/StatsCollector.hpp"
#include "helpers.hpp"

#include <catch.hpp>

#include <stdexcept>
#include <string>
#include <vector>

using json = nlohmann::json;
using namespace observertc::clientmonitor;

namespace
{
	/** Appends what happened, in order, to a shared log. */
	struct Journal
	{
		std::vector<std::string> entries;
	};

	class RecordingAdapter : public StatsAdapter
	{
	public:
		RecordingAdapter(std::string name, Journal& journal) : name(std::move(name)), journal(journal)
		{
		}

		const std::string& GetName() const override
		{
			return this->name;
		}

		void Adapt(const std::string& peerConnectionId, json& stats) override
		{
			this->journal.entries.push_back(this->name + ":" + peerConnectionId);
			this->seen.push_back(stats);

			if (this->rename)
			{
				// Something only an adapter could have done: the monitors must see it.
				for (auto& stat : stats)
				{
					if (stat["type"] == "inbound-rtp")
					{
						stat["trackIdentifier"] = "renamed-audio";
					}
				}
			}
			if (this->fail)
			{
				throw std::runtime_error("adapter failure");
			}
		}

		std::string name;
		Journal& journal;
		std::vector<json> seen;
		bool rename{ false };
		bool fail{ false };
	};

	struct JournalListener : public helpers::RecordingListener
	{
		explicit JournalListener(Journal& journal) : journal(journal)
		{
		}

		void OnRawStats(ClientMonitor*, const std::string& peerConnectionId, const json& rawStats) override
		{
			this->journal.entries.push_back("raw:" + peerConnectionId);
			this->raw.push_back(rawStats);
		}

		Journal& journal;
		std::vector<json> raw;
	};

	struct Fixture
	{
		Fixture()
		  : listener(journal),
		    monitor(helpers::ManualConfig(), &listener),
		    collectorA(std::make_shared<helpers::ScriptedStatsCollector>()),
		    collectorB(std::make_shared<helpers::ScriptedStatsCollector>())
		{
			pcA = monitor.AddPeerConnection(collectorA, "pc-a");
			pcB = monitor.AddPeerConnection(collectorB, "pc-b");
		}

		void Collect()
		{
			collectorA->next = helpers::BuildReport(c);
			collectorB->next = helpers::BuildReport(c);
			monitor.Collect();
			c.timestamp += 1000;
			clock.Advance(1000);
		}

		helpers::FakeClock clock;
		Journal journal;
		JournalListener listener;
		ClientMonitor monitor;
		std::shared_ptr<helpers::ScriptedStatsCollector> collectorA;
		std::shared_ptr<helpers::ScriptedStatsCollector> collectorB;
		PeerConnectionMonitor* pcA{ nullptr };
		PeerConnectionMonitor* pcB{ nullptr };
		helpers::Counters c;
	};
} // namespace

TEST_CASE("Stats adapters on the client monitor", "[adapters]")
{
	Fixture f;

	SECTION("the client monitor starts with the libwebrtc adapter")
	{
		REQUIRE(f.monitor.GetStatsAdapter("libWebRtcStatsAdapter") != nullptr);

		f.Collect();

		// BuildReport() gives the inbound stream `mediaType`, not `kind`: only the adapter makes it
		// a stream the monitors accept.
		REQUIRE(f.pcA->GetInboundRtpMonitor(1111) != nullptr);
		REQUIRE(f.pcA->GetInboundRtpMonitor(1111)->GetStats().kind == "audio");
	}

	SECTION("every peer connection's raw report is reported, then adapted in order, with its id")
	{
		auto first  = std::make_unique<RecordingAdapter>("first", f.journal);
		auto second = std::make_unique<RecordingAdapter>("second", f.journal);

		f.monitor.AddStatsAdapter(std::move(first));
		f.monitor.AddStatsAdapter(std::move(second));
		f.Collect();

		REQUIRE(
		  f.journal.entries ==
		  std::vector<std::string>{ "raw:pc-a", "first:pc-a", "second:pc-a", "raw:pc-b", "first:pc-b", "second:pc-b" });
	}

	SECTION("the raw report is the collector's; the adapters see it after the libwebrtc adapter")
	{
		f.monitor.AddStatsAdapter(std::make_unique<RecordingAdapter>("after", f.journal));
		f.Collect();

		auto* after = dynamic_cast<RecordingAdapter*>(f.monitor.GetStatsAdapter("after"));

		REQUIRE(f.listener.raw.front() == f.collectorA->next);
		REQUIRE(f.listener.raw.front().dump().find("\"mediaType\"") != std::string::npos);
		REQUIRE(after->seen.front().dump().find("\"mediaType\"") == std::string::npos);
	}

	SECTION("the monitors read the adapted report")
	{
		auto adapter = std::make_unique<RecordingAdapter>("rename", f.journal);

		adapter->rename = true;
		f.monitor.AddStatsAdapter(std::move(adapter));
		f.Collect();

		REQUIRE(f.pcA->GetInboundRtpMonitor(1111)->GetStats().trackIdentifier == "renamed-audio");
	}

	SECTION("a report the application accepts takes the same path")
	{
		f.monitor.AddStatsAdapter(std::make_unique<RecordingAdapter>("only", f.journal));
		f.pcB->Accept(helpers::BuildReport(f.c));

		REQUIRE(f.journal.entries == std::vector<std::string>{ "raw:pc-b", "only:pc-b" });
		REQUIRE(f.pcB->GetInboundRtpMonitor(1111)->GetStats().kind == "audio");
	}

	SECTION("a failing adapter is skipped and the report is still accepted")
	{
		auto failing = std::make_unique<RecordingAdapter>("failing", f.journal);

		failing->fail = true;
		f.monitor.AddStatsAdapter(std::move(failing));
		f.monitor.AddStatsAdapter(std::make_unique<RecordingAdapter>("next", f.journal));
		f.Collect();

		REQUIRE(f.pcA->GetInboundRtpMonitor(1111) != nullptr);
		REQUIRE(f.journal.entries[2] == "next:pc-a");
	}

	SECTION("adding a name twice keeps the first; removing stops it")
	{
		f.monitor.AddStatsAdapter(std::make_unique<RecordingAdapter>("dup", f.journal));
		f.monitor.AddStatsAdapter(std::make_unique<RecordingAdapter>("dup", f.journal));
		f.Collect();

		REQUIRE(f.journal.entries == std::vector<std::string>{ "raw:pc-a", "dup:pc-a", "raw:pc-b", "dup:pc-b" });

		REQUIRE(f.monitor.RemoveStatsAdapter("dup"));
		REQUIRE_FALSE(f.monitor.RemoveStatsAdapter("dup"));
		REQUIRE(f.monitor.GetStatsAdapter("dup") == nullptr);

		f.journal.entries.clear();
		f.Collect();

		REQUIRE(f.journal.entries == std::vector<std::string>{ "raw:pc-a", "raw:pc-b" });
	}

	SECTION("without the libwebrtc adapter the monitors get the report as collected")
	{
		REQUIRE(f.monitor.RemoveStatsAdapter("libWebRtcStatsAdapter"));

		f.Collect();

		// The inbound stream has `mediaType` but no `kind`, so no monitor accepts it.
		REQUIRE(f.pcA->GetInboundRtpMonitor(1111) == nullptr);
	}

	SECTION("a removed peer connection's report is neither reported nor accepted")
	{
		f.monitor.RemovePeerConnection("pc-b");
		f.Collect();

		REQUIRE(f.journal.entries == std::vector<std::string>{ "raw:pc-a" });
	}

	SECTION("the collected event carries the adapted reports")
	{
		struct Collected : public JournalListener
		{
			using JournalListener::JournalListener;

			void OnStatsCollected(ClientMonitor*, const StatsCollectedEvent& event) override
			{
				this->collected = event.collectedStats;
			}

			std::vector<std::pair<std::string, json>> collected;
		};

		Journal journal;
		Collected listener(journal);
		helpers::FakeClock clock;
		ClientMonitor monitor(helpers::ManualConfig(), &listener);
		auto collector = std::make_shared<helpers::ScriptedStatsCollector>();

		monitor.AddPeerConnection(collector, "pc-1");
		collector->next = helpers::BuildReport(helpers::Counters{});
		monitor.Collect();

		REQUIRE(listener.collected.size() == 1);
		REQUIRE(listener.collected[0].first == "pc-1");
		REQUIRE(listener.collected[0].second.dump().find("\"mediaType\"") == std::string::npos);
		REQUIRE(listener.raw[0].dump().find("\"mediaType\"") != std::string::npos);
	}
}

TEST_CASE("The detached sender adapter only corrects the peer connections it was told about", "[adapters]")
{
	helpers::FakeClock clock;
	helpers::RecordingListener listener;
	ClientMonitor monitor(helpers::ManualConfig(), &listener);
	auto sending   = std::make_shared<helpers::ScriptedStatsCollector>();
	auto untouched = std::make_shared<helpers::ScriptedStatsCollector>();
	auto* pcSend   = monitor.AddPeerConnection(sending, "pc-send");
	auto* pcOther  = monitor.AddPeerConnection(untouched, "pc-other");

	monitor.AddStatsAdapter(std::make_unique<DetachedSenderStatsAdapter>());
	{
		std::lock_guard<std::recursive_mutex> lock(monitor.GetMutex());

		// pc-send has a sender with mid "9" only, so its stream (mid "0") carries no track.
		dynamic_cast<DetachedSenderStatsAdapter*>(monitor.GetStatsAdapter(DetachedSenderStatsAdapter::Name))
		  ->SetSenderTrack("pc-send", "9", true);
	}

	helpers::Counters c;
	auto report = [&]()
	{
		auto r = helpers::BuildReport(c);

		for (auto& stat : r)
		{
			if (stat["type"] == "outbound-rtp")
			{
				stat["mid"]           = "0";
				stat["mediaSourceId"] = "MS1";
			}
		}

		return r;
	};

	pcSend->AddMediaTrack(std::make_shared<BasicMediaTrack>("local-video", "video"));
	pcOther->AddMediaTrack(std::make_shared<BasicMediaTrack>("local-video", "video"));

	for (int i = 0; i < 3; ++i)
	{
		c.outboundBytes += 1000;
		sending->next   = report();
		untouched->next = report();
		monitor.Collect();
		c.timestamp += 1000;
		clock.Advance(1000);
	}

	REQUIRE(pcSend->GetOutboundRtpMonitor(2222)->GetStats().mediaSourceId == std::nullopt);
	REQUIRE(pcSend->GetOutboundTrackMonitor("local-video")->GetOutboundRtps().empty());
	REQUIRE(pcOther->GetOutboundRtpMonitor(2222)->GetStats().mediaSourceId == "MS1");
	REQUIRE(pcOther->GetOutboundTrackMonitor("local-video")->GetOutboundRtps().size() == 1);
}
