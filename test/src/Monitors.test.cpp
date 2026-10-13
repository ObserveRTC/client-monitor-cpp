#include "clientmonitor/ClientMonitor.hpp"
#include "helpers.hpp"

#include <catch.hpp>

using json = nlohmann::json;
using namespace clientmonitor;

TEST_CASE("PeerConnectionMonitor", "[monitors]")
{
	helpers::FakeClock clock;
	auto collector = std::make_shared<helpers::ScriptedStatsCollector>();
	ClientMonitor monitor(helpers::ManualConfig());
	auto* pc = monitor.AddPeerConnection(collector, "pc-1");
	helpers::Counters c;

	REQUIRE(pc != nullptr);
	REQUIRE(monitor.AddPeerConnection(collector, "pc-1") == nullptr);

	collector->next = helpers::BuildReport(c);
	monitor.Collect();

	SECTION("builds a monitor per stats object")
	{
		REQUIRE(pc->GetCodecs().size() == 2);
		REQUIRE(pc->GetInboundRtps().size() == 1);
		REQUIRE(pc->GetOutboundRtps().size() == 1);
		REQUIRE(pc->GetRemoteInboundRtps().size() == 1);
		REQUIRE(pc->GetMediaSources().size() == 1);
		REQUIRE(pc->GetIceTransports().size() == 1);
		REQUIRE(pc->GetIceCandidates().size() == 2);
		REQUIRE(pc->GetLocalIceCandidates().size() == 1);
		REQUIRE(pc->GetCertificates().size() == 1);
		REQUIRE(pc->GetSelectedIceCandidatePairs().size() == 1);
	}

	SECTION("walks the graph")
	{
		auto* outbound = pc->GetOutboundRtpMonitor(2222);

		REQUIRE(outbound->GetCodec()->GetStats().mimeType == "video/VP8");
		REQUIRE(outbound->GetMediaSource()->GetId() == "MS1");
		REQUIRE(outbound->GetRemoteInboundRtp()->GetOutboundRtp() == outbound);
		REQUIRE(
		  outbound->GetSelectedCandidatePair()->GetLocalCandidate()->GetAddressFamily() ==
		  std::string("ipv4"));
		REQUIRE(outbound->GetSelectedCandidatePair()->GetPathKind() == "direct");
		REQUIRE(outbound->GetSelectedCandidatePair()->GetTuple() == "10.0.0.1:50000:203.0.113.7:3478:udp");
	}

	SECTION("derives rates from consecutive reports")
	{
		c.timestamp += 1000;
		c.inboundBytes    = 4000;
		c.inboundPackets  = 90;
		c.inboundLost     = 10;
		c.outboundBytes   = 125000;
		c.outboundPackets = 100;
		collector->next   = helpers::BuildReport(c);
		monitor.Collect();

		auto* inbound = pc->GetInboundRtpMonitor(1111);

		REQUIRE(*inbound->GetMetrics().bitrate == Approx(32000));
		REQUIRE(*inbound->GetMetrics().deltaFractionLost == Approx(0.1));
		REQUIRE(*pc->GetOutboundRtpMonitor(2222)->GetMetrics().bitrate == Approx(1000000));
		REQUIRE(pc->GetMetrics().sendingVideoBitrate == Approx(1000000));
		REQUIRE(pc->GetMetrics().receivingAudioBitrate == Approx(32000));
		REQUIRE(*pc->GetMetrics().deltaTime == Approx(1000));
		REQUIRE(*pc->GetAvgRttInSec() == Approx(0.05));
		REQUIRE(pc->GetMetrics().iceState == std::string("connected"));
		REQUIRE(*pc->GetMetrics().totalAvailableOutgoingBitrate == Approx(1500000));
		REQUIRE(monitor.GetMetrics().sendingVideoBitrate == Approx(1000000));
	}

	SECTION("drops a monitor whose report stopped arriving")
	{
		auto report = helpers::BuildReport(c);

		report.erase(report.size() - 1); // the certificate
		c.timestamp += 1000;
		collector->next = report;
		monitor.Collect();

		REQUIRE(pc->GetCertificates().empty());
	}

	SECTION("matches a track to its stats, even when the track arrives first")
	{
		auto remoteAudio = std::make_shared<BasicMediaTrack>("remote-audio", "audio");
		auto localVideo  = std::make_shared<BasicMediaTrack>("local-video", "video");
		auto future      = std::make_shared<BasicMediaTrack>("future-track", "audio");

		pc->AddMediaTrack(remoteAudio);
		pc->AddMediaTrack(localVideo);
		pc->AddMediaTrack(future);

		REQUIRE(pc->GetInboundTrackMonitor("remote-audio") != nullptr);
		REQUIRE(pc->GetOutboundTrackMonitor("local-video") != nullptr);
		REQUIRE(pc->GetInboundTrackMonitor("future-track") == nullptr);

		auto report = helpers::BuildReport(c);

		report.push_back({
		  { "type",            "inbound-rtp"      },
		  { "id",		          "IA3333"           },
		  { "timestamp",       c.timestamp + 1000 },
		  { "ssrc",            3333               },
		  { "kind",            "audio"            },
		  { "trackIdentifier", "future-track"     }
    });
		c.timestamp += 1000;
		collector->next = report;
		monitor.Collect();

		REQUIRE(pc->GetInboundTrackMonitor("future-track") != nullptr);
		REQUIRE(pc->GetOutboundTrackMonitor("local-video")->GetOutboundRtps().size() == 1);
		REQUIRE(monitor.GetInboundTrackMonitor("remote-audio")->GetKind() == "audio");
	}

	SECTION("a track context declared early is applied when the track appears")
	{
		InboundTrackContext context;

		context.paused = true;
		monitor.SetInboundTrackContext("remote-audio", context);

		InboundTrackContext more;

		more.linkedVideoTrackId = "remote-video";
		monitor.SetInboundTrackContext("remote-audio", more);

		pc->AddMediaTrack(std::make_shared<BasicMediaTrack>("remote-audio", "audio"));

		auto* track = pc->GetInboundTrackMonitor("remote-audio");

		REQUIRE(track->IsPaused());
		REQUIRE(track->GetContext().linkedVideoTrackId == std::string("remote-video"));
	}

	SECTION("static ICE transport metadata ships on change only")
	{
		// The sample created by Collect() above already carried it.
		auto unchanged = pc->CreateSample();

		REQUIRE_FALSE(unchanged.iceTransports->at(0).iceRole.has_value());
		REQUIRE(unchanged.iceTransports->at(0).iceState == std::string("connected"));

		// An ICE restart changes the ufrag, so the metadata ships again.
		c.timestamp += 1000;
		c.ufrag         = "ufrag2";
		collector->next = helpers::BuildReport(c);
		monitor.Collect();

		auto config                                 = helpers::ManualConfig();
		config.sendIceTransportMetadataOnChangeOnly = false;
		ClientMonitor everySample(config);
		auto* other = everySample.AddPeerConnection(collector, "pc-2");

		everySample.Collect();

		REQUIRE(other->CreateSample().iceTransports->at(0).iceRole == std::string("controlling"));
		REQUIRE(other->CreateSample().iceTransports->at(0).iceLocalUsernameFragment == std::string("ufrag2"));
	}

	SECTION("close drops everything")
	{
		REQUIRE(monitor.RemovePeerConnection("pc-1"));
		REQUIRE(monitor.GetPeerConnections().empty());
	}
}
