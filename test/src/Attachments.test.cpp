#include "clientmonitor/ClientMonitor.hpp"
#include "clientmonitor/schema/ClientEventTypes.hpp"
#include "helpers.hpp"

#include <catch.hpp>

using json = nlohmann::json;
using namespace observertc::clientmonitor;

namespace
{
	json WithDataChannel(json report, double timestamp, int64_t identifier)
	{
		report.push_back(
		  {
		    { "type",		              "data-channel" },
		    { "id",		                "D1"           },
		    { "timestamp",             timestamp      },
		    { "label",                 "signaling"    },
		    { "dataChannelIdentifier", identifier     },
		    { "state",                 "open"         },
		    { "bytesSent",             0              },
		    { "bytesReceived",         0              }
    });

		return report;
	}

	const PeerConnectionSample& OnlyPeerConnection(const ClientSample& sample)
	{
		REQUIRE(sample.peerConnections.has_value());
		REQUIRE(sample.peerConnections->size() == 1);

		return sample.peerConnections->front();
	}
} // namespace

TEST_CASE("Attachments", "[attachments]")
{
	helpers::FakeClock clock;
	auto collector = std::make_shared<helpers::ScriptedStatsCollector>();
	ClientMonitor monitor(helpers::ManualConfig());
	auto* pc = monitor.AddPeerConnection(
	  collector,
	  "transport-1",
	  {
	    { "direction", "send" }
  });
	helpers::Counters c;

	REQUIRE(pc != nullptr);

	SECTION("the peer connection carries its attachments and id")
	{
		collector->next = helpers::BuildReport(c);
		monitor.Collect();

		const auto sample    = monitor.CreateSample();
		const auto& pcSample = OnlyPeerConnection(*sample);

		REQUIRE(pcSample.peerConnectionId == "transport-1");
		REQUIRE(
		  pcSample.attachments == json(
		                            {
		                              { "direction", "send" }
    }));
	}

	SECTION("client attachments merge, and a null value removes a key")
	{
		monitor.SetAttachments(
		  {
		    { "platform", "native" }
    });
		monitor.UpdateAttachments(
		  {
		    { "senderSideCoupledCongestion", { { "enabled", true }, { "enforcedPolicy", "v1" } } }
    });
		REQUIRE(monitor.GetAttachments()["platform"] == "native");
		REQUIRE(monitor.GetAttachments()["senderSideCoupledCongestion"]["enforcedPolicy"] == "v1");

		monitor.UpdateAttachments(
		  {
		    { "senderSideCoupledCongestion", nullptr }
    });

		const auto sample = monitor.CreateSample();

		REQUIRE(
		  sample->attachments == json(
		                           {
		                             { "platform", "native" }
    }));
	}

	SECTION("a stats object keeps its attachments across reports")
	{
		collector->next = helpers::BuildReport(c);
		monitor.Collect();

		pc->GetOutboundRtpMonitor(2222)->UpdateAttachments(
		  {
		    { "producerId", "producer-1" }
    });

		c.timestamp += 1000;
		collector->next = helpers::BuildReport(c);
		monitor.Collect();

		const auto sample    = monitor.CreateSample();
		const auto& pcSample = OnlyPeerConnection(*sample);

		REQUIRE(pcSample.outboundRtps->size() == 1);
		REQUIRE(
		  pcSample.outboundRtps->front().attachments == json(
		                                                  {
		                                                    { "producerId", "producer-1" }
    }));
		// Only what was attached; the inbound stream was left alone.
		REQUIRE(pcSample.inboundRtps->front().attachments.is_null());
		// The ICE transport overrides CreateSample() and must still carry them.
		pc->GetIceTransports().front()->UpdateAttachments(
		  {
		    { "role", "main" }
    });
		REQUIRE(
		  OnlyPeerConnection(*monitor.CreateSample()).iceTransports->front().attachments ==
		  json(
		    {
		      { "role", "main" }
    }));
	}

	SECTION("data channel attachments bind by SCTP stream id, declared before or after the stats")
	{
		pc->SetDataChannelAttachments(
		  3,
		  {
		    { "dataProducerId", "dp-1" }
    });
		collector->next = WithDataChannel(helpers::BuildReport(c), c.timestamp, 3);
		monitor.Collect();

		auto dataChannels = OnlyPeerConnection(*monitor.CreateSample()).dataChannels;

		REQUIRE(dataChannels->size() == 1);
		REQUIRE(
		  dataChannels->front().attachments == json(
		                                         {
		                                           { "dataProducerId", "dp-1" }
    }));

		// Declared later: merged into the live monitor.
		pc->SetDataChannelAttachments(
		  3,
		  {
		    { "dataConsumerId", "dc-1" }
    });
		c.timestamp += 1000;
		collector->next = WithDataChannel(helpers::BuildReport(c), c.timestamp, 3);
		monitor.Collect();

		dataChannels = OnlyPeerConnection(*monitor.CreateSample()).dataChannels;

		REQUIRE(
		  dataChannels->front().attachments ==
		  json(
		    {
		      { "dataProducerId", "dp-1" },
          { "dataConsumerId", "dc-1" }
    }));
	}

	SECTION("a data channel with another identifier gets nothing")
	{
		pc->SetDataChannelAttachments(
		  5,
		  {
		    { "dataProducerId", "dp-1" }
    });
		collector->next = WithDataChannel(helpers::BuildReport(c), c.timestamp, 3);
		monitor.Collect();

		REQUIRE(OnlyPeerConnection(*monitor.CreateSample()).dataChannels->front().attachments.is_null());
	}

	SECTION("track attachments reach the track samples")
	{
		auto localVideo  = std::make_shared<BasicMediaTrack>("local-video", "video");
		auto remoteAudio = std::make_shared<BasicMediaTrack>("remote-audio", "audio");

		pc->AddMediaTrack(
		  localVideo,
		  {
		    { "producerId", "p-1" }
    });
		pc->AddMediaTrack(
		  remoteAudio,
		  {
		    { "producerId", "p-remote" },
        { "consumerId", "c-1"      }
    });
		collector->next = helpers::BuildReport(c);
		monitor.Collect();

		const auto sample    = monitor.CreateSample();
		const auto& pcSample = OnlyPeerConnection(*sample);

		REQUIRE(
		  pcSample.outboundTracks->front().attachments == json(
		                                                    {
		                                                      { "producerId", "p-1" }
    }));
		REQUIRE(
		  pcSample.inboundTracks->front().attachments ==
		  json(
		    {
		      { "producerId", "p-remote" },
          { "consumerId", "c-1"      }
    }));
	}
}

TEST_CASE("Meta data with explicit fields", "[attachments]")
{
	helpers::FakeClock clock;
	ClientMonitor monitor(helpers::ManualConfig());
	ClientMetaData sdp;

	sdp.type             = ClientMetaTypes::LOCAL_SDP;
	sdp.payload          = "v=0\r\n";
	sdp.peerConnectionId = "transport-1";
	monitor.AddMetaData(sdp);

	const auto sample = monitor.CreateSample();

	REQUIRE(sample->clientMetaItems.has_value());
	REQUIRE(sample->clientMetaItems->size() == 1);
	REQUIRE(
	  sample->clientMetaItems->front().peerConnectionId == std::optional<std::string>("transport-1"));
	REQUIRE(sample->clientMetaItems->front().payload == "v=0\r\n");
	REQUIRE(sample->clientMetaItems->front().timestamp == static_cast<double>(clock.Now()));
}

TEST_CASE("Telemetry before the call is delivered in its first sample", "[attachments]")
{
	helpers::FakeClock clock;
	auto config = helpers::ManualConfig();

	// The way a client joins: no sampling until the call is up, and no CLIENT_JOINED per
	// monitor, since one monitor spans several calls.
	config.samplingPeriodInMs           = 0;
	config.bufferingEventsForSamples    = true;
	config.addClientJointEventOnCreated = false;
	config.addClientLeftEventOnClose    = false;

	ClientMonitor monitor(config);
	helpers::RecordingListener listener;

	monitor.AddListener(&listener);
	monitor.AddMetaData(
	  "MEDIA_DEVICES_AUDIOINPUT_SNAPSHOT",
	  {
	    { "seq", 0 }
  });
	monitor.AddIssue(
	  "producer-creation-failed",
	  {
	    { "kind", "audio" }
  });
	monitor.Collect();
	REQUIRE(listener.samples.empty());

	monitor.AddClientJoinEvent();
	monitor.SetSamplingPeriod(1000);
	monitor.Collect();

	REQUIRE(listener.samples.size() == 1);

	const auto& sample = listener.samples.front();

	REQUIRE(sample.clientMetaItems->size() == 1);
	REQUIRE(sample.clientIssues->size() == 1);
	REQUIRE(sample.clientEvents->size() == 1);
	REQUIRE(sample.clientEvents->front().type == ClientEventTypes::CLIENT_JOINED);

	monitor.RemoveListener(&listener);
}
