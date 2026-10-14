/**
 * Wiring client-monitor-cpp into a libmediasoupclient application, end to end: what a native
 * client has to call so its samples carry the same attachments and events as the web client's.
 *
 * Needs -DCLIENTMONITOR_MEDIASOUPCLIENT=ON. CI compiles this file against libmediasoupclient and
 * a prebuilt libwebrtc (test/bindings), so the calls below are checked against the real API; it is
 * not linked or run there. Read it top to bottom: every step says which part of the sample it
 * fills in.
 */

#include <clientmonitor/ClientMonitor.hpp>
#include <clientmonitor/mediasoupclient/MediasoupClientBinding.hpp>
#include <clientmonitor/schema/ClientEventTypes.hpp>

#include <Device.hpp>

#include <functional>
#include <memory>
#include <string>

namespace cm = observertc::clientmonitor;
using json   = nlohmann::json;

/**
 * One per process. Samples go out through `send`, the application's own signaling (the web client
 * sends them as the `client-sample` command on its data signaling).
 */
class CallTelemetry : public cm::ClientMonitor::Listener
{
public:
	explicit CallTelemetry(std::function<void(const json&)> send) : send(std::move(send))
	{
		cm::ClientMonitorConfig config;

		// Collect every 5 s, but sample nothing until a call is up (SetSamplingPeriod in Joined()).
		// What is reported before that (device failures, snapshots) is held for the first sample.
		config.collectingPeriodInMs      = 5000;
		config.samplingPeriodInMs        = 0;
		config.bufferingEventsForSamples = true;
		// One monitor spans several calls: CLIENT_JOINED / CLIENT_LEFT are added per call below.
		config.addClientJointEventOnCreated = false;
		config.addClientLeftEventOnClose    = false;

		this->monitor = std::make_unique<cm::ClientMonitor>(config, this);
	}

	cm::ClientMonitor& GetMonitor()
	{
		return *this->monitor;
	}

	/* Call lifecycle. */

	void Joined(const std::string& clientId, const std::string& callId)
	{
		this->monitor->SetClientId(clientId);
		this->monitor->SetCallId(callId);
		this->monitor->AddClientJoinEvent();
		this->monitor->SetSamplingPeriod(5000);
	}

	void Left(bool rejoining)
	{
		this->monitor->AddClientLeftEvent(
		  rejoining ? json{ { "reason", "livecall-rejoin" } } : json::object());
		// One last sample with CLIENT_LEFT in it, then quiet until the next call.
		this->monitor->CreateSample();
		this->monitor->SetSamplingPeriod(0);
	}

	/* Transports: peerConnections[].peerConnectionId and attachments.direction. */

	mediasoupclient::SendTransport* CreateSendTransport(
	  mediasoupclient::Device& device,
	  mediasoupclient::SendTransport::Listener* appListener,
	  const json& params)
	{
		this->sendMonitor = std::make_unique<cm::mediasoupclient::TransportMonitor>(this->monitor.get());
		// The decorator reports connection state and forwards everything to the app's listener.
		this->sendListener = std::make_unique<cm::mediasoupclient::MonitoredSendTransportListener>(
		  this->sendMonitor.get(), appListener);

		auto* transport = device.CreateSendTransport(
		  this->sendListener.get(),
		  params["id"].get<std::string>(),
		  params["iceParameters"],
		  params["iceCandidates"],
		  params["dtlsParameters"],
		  params["sctpParameters"]);

		// The transport id becomes the peerConnectionId; direction "send" is attached.
		this->sendMonitor->Bind(transport);

		return transport;
	}

	mediasoupclient::RecvTransport* CreateRecvTransport(
	  mediasoupclient::Device& device,
	  mediasoupclient::RecvTransport::Listener* appListener,
	  const json& params)
	{
		this->recvMonitor = std::make_unique<cm::mediasoupclient::TransportMonitor>(this->monitor.get());
		this->recvListener = std::make_unique<cm::mediasoupclient::MonitoredRecvTransportListener>(
		  this->recvMonitor.get(), appListener);

		auto* transport = device.CreateRecvTransport(
		  this->recvListener.get(),
		  params["id"].get<std::string>(),
		  params["iceParameters"],
		  params["iceCandidates"],
		  params["dtlsParameters"],
		  params["sctpParameters"]);

		this->recvMonitor->Bind(transport);

		return transport;
	}

	/* Producers: outboundTracks[].attachments.producerId, PRODUCER_* events. */

	mediasoupclient::Producer* Produce(
	  mediasoupclient::SendTransport* transport,
	  mediasoupclient::Producer::Listener* listener,
	  webrtc::MediaStreamTrackInterface* track,
	  const std::string& contentType)
	{
		// Declared before the stats appear: a screen share is not judged like a camera.
		cm::OutboundTrackContext context;

		context.contentType = contentType;
		this->monitor->SetOutboundTrackContext(track->id(), context);

		auto* producer = transport->Produce(listener, track, nullptr, nullptr, nullptr);

		this->sendMonitor->OnProducerCreated(producer);

		return producer;
	}

	void PauseProducer(mediasoupclient::Producer* producer)
	{
		producer->Pause();
		this->sendMonitor->OnProducerPaused(producer);
	}

	void ResumeProducer(mediasoupclient::Producer* producer)
	{
		producer->Resume();
		this->sendMonitor->OnProducerResumed(producer);
	}

	void ReplaceTrack(mediasoupclient::Producer* producer, webrtc::MediaStreamTrackInterface* track)
	{
		producer->ReplaceTrack(track);
		// The new track keeps the producerId attachment; the old one is dropped.
		this->sendMonitor->OnProducerTrackReplaced(producer);
	}

	void CloseProducer(mediasoupclient::Producer* producer)
	{
		// Before Close(): the binding reads the producer's ids and track.
		this->sendMonitor->OnProducerClosed(producer);
		producer->Close();
	}

	/* Consumers: inboundTracks[].attachments { producerId, consumerId }, CONSUMER_* events. */

	mediasoupclient::Consumer* Consume(
	  mediasoupclient::RecvTransport* transport,
	  mediasoupclient::Consumer::Listener* listener,
	  const json& params,
	  bool screenShare)
	{
		auto rtpParameters = params["rtpParameters"];
		auto* consumer     = transport->Consume(
      listener,
      params["id"].get<std::string>(),
      params["producerId"].get<std::string>(),
      params["kind"].get<std::string>(),
      &rtpParameters);

		cm::InboundTrackContext context;

		context.contentType = screenShare ? "screenshare" : "camera";

		if (screenShare)
		{
			context.motionType = "lowmotion";
		}

		this->monitor->SetInboundTrackContext(consumer->GetTrack()->id(), context);
		this->recvMonitor->OnConsumerCreated(consumer);

		return consumer;
	}

	// The SFU's producerpause / producerresume notifications for a consumer's producer.
	void RemoteProducerPaused(mediasoupclient::Consumer* consumer, bool paused)
	{
		if (paused)
		{
			this->recvMonitor->OnConsumerProducerPaused(consumer);
		}
		else
		{
			this->recvMonitor->OnConsumerProducerResumed(consumer);
		}
	}

	void CloseConsumer(mediasoupclient::Consumer* consumer)
	{
		this->recvMonitor->OnConsumerClosed(consumer);
		consumer->Close();
	}

	/* Data channels: dataChannels[].attachments, matched by SCTP stream id. */

	mediasoupclient::DataProducer* ProduceData(
	  mediasoupclient::SendTransport* transport, mediasoupclient::DataProducer::Listener* listener)
	{
		auto* dataProducer = transport->ProduceData(listener, "signaling");

		this->sendMonitor->OnDataProducerCreated(dataProducer);

		return dataProducer;
	}

	mediasoupclient::DataConsumer* ConsumeData(
	  mediasoupclient::RecvTransport* transport,
	  mediasoupclient::DataConsumer::Listener* listener,
	  const json& params)
	{
		auto* dataConsumer = transport->ConsumeData(
		  listener,
		  params["id"].get<std::string>(),
		  params["dataProducerId"].get<std::string>(),
		  params["sctpStreamParameters"]["streamId"].get<uint16_t>(),
		  params["label"].get<std::string>());

		this->recvMonitor->OnDataConsumerCreated(dataConsumer);

		return dataConsumer;
	}

	/* Application telemetry: everything the web client adds on top of the stats. */

	void ReportExamples()
	{
		// ClientSample.attachments, one key per owner; null removes the key again.
		this->monitor->UpdateAttachments(
		  {
		    { "senderSideCoupledCongestion", { { "enabled", true }, { "enforcedPolicy", "v1" } } }
    });

		// clientEvents[]
		this->monitor->AddEvent(
		  "RECORDING_SESSION_CREATED",
		  {
		    { "takeId",        "take-1"        },
		    { "recordingMode", "live"          },
		    { "recordingType", "audioAndVideo" },
		    { "version",       "3.1.0"         }
    });

		// clientIssues[]
		this->monitor->AddIssue(
		  "producer-creation-failed",
		  {
		    { "kind",  "audio"            },
        { "label", "camera"           },
        { "error", "NotReadableError" }
    });

		// clientMetaItems[], with a payload that is not an object.
		cm::ClientMetaData sdp;

		sdp.type             = cm::ClientMetaTypes::LOCAL_SDP;
		sdp.payload          = "v=0\r\n...";
		sdp.peerConnectionId = this->sendMonitor->GetTransportId();
		this->monitor->AddMetaData(sdp);

		// extensionStats[], polled on every collection.
		this->monitor->AddExtensionStatsProvider(
		  []() -> std::optional<cm::ClientMonitor::ExtensionStatsInput>
		  {
			  return cm::ClientMonitor::ExtensionStatsInput{ "recorderServiceStats",
				                                               { { "state", "recording" } },
				                                               std::nullopt };
		  });
	}

	/* ClientMonitor::Listener */

	void OnSampleCreated(cm::ClientMonitor* /*monitor*/, const cm::ClientSample& sample) override
	{
		// Runs with the monitor's lock held on the collecting thread: hand off, do not block.
		this->send(sample.ToJson());
	}

private:
	std::function<void(const json&)> send;
	std::unique_ptr<cm::ClientMonitor> monitor;
	std::unique_ptr<cm::mediasoupclient::TransportMonitor> sendMonitor;
	std::unique_ptr<cm::mediasoupclient::TransportMonitor> recvMonitor;
	std::unique_ptr<cm::mediasoupclient::MonitoredSendTransportListener> sendListener;
	std::unique_ptr<cm::mediasoupclient::MonitoredRecvTransportListener> recvListener;
};

// Keeps the class from being discarded unused when this file is compiled on its own.
std::unique_ptr<CallTelemetry> MakeCallTelemetry(std::function<void(const json&)> send)
{
	return std::make_unique<CallTelemetry>(std::move(send));
}
