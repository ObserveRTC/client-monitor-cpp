#define CM_CLASS "MediasoupClientBinding"

#include "clientmonitor/mediasoupclient/MediasoupClientBinding.hpp"
#include "clientmonitor/Logger.hpp"
#include "clientmonitor/libwebrtc/LibWebRtcBinding.hpp"
#include "clientmonitor/schema/ClientEventTypes.hpp"

using json = nlohmann::json;

namespace clientmonitor
{
	namespace mediasoupclient
	{
		namespace
		{
			std::string TrackIdOf(webrtc::MediaStreamTrackInterface* track)
			{
				return track ? track->id() : std::string();
			}

			std::shared_ptr<MediaTrack> Wrap(webrtc::MediaStreamTrackInterface* track)
			{
				if (!track)
				{
					return nullptr;
				}

				return std::make_shared<libwebrtc::WebRtcMediaTrack>(
				  webrtc::scoped_refptr<webrtc::MediaStreamTrackInterface>(track));
			}
		} // namespace

		/* TransportStatsCollector */

		TransportStatsCollector::TransportStatsCollector(::mediasoupclient::Transport* transport)
		  : transport(transport)
		{
		}

		json TransportStatsCollector::GetStats()
		{
			auto* transport = this->transport.load();

			if (!transport || transport->IsClosed())
			{
				return json::array();
			}

			try
			{
				return transport->GetStats();
			}
			catch (const std::exception& error)
			{
				CM_WARN("transport GetStats() failed: %s", error.what());

				return json::array();
			}
		}

		void TransportStatsCollector::Detach()
		{
			this->transport.store(nullptr);
		}

		/* TransportMonitor */

		TransportMonitor::TransportMonitor(ClientMonitor* clientMonitor, const json& attachments)
		  : clientMonitor(clientMonitor), attachments(attachments)
		{
		}

		TransportMonitor::~TransportMonitor()
		{
			this->Close();
		}

		PeerConnectionMonitor* TransportMonitor::Bind(::mediasoupclient::Transport* transport)
		{
			if (!transport || this->collector)
			{
				return nullptr;
			}

			this->transportId = transport->GetId();
			this->collector   = std::make_shared<TransportStatsCollector>(transport);

			auto* monitor = this->clientMonitor->AddPeerConnection(
			  this->collector, this->transportId, this->attachments);

			this->FireEvent(ClientEventTypes::PEER_CONNECTION_OPENED, json::object());

			return monitor;
		}

		void TransportMonitor::Close()
		{
			if (!this->collector)
			{
				return;
			}

			this->FireEvent(ClientEventTypes::PEER_CONNECTION_CLOSED, json::object());
			this->collector->Detach();
			this->clientMonitor->RemovePeerConnection(this->transportId);
			this->collector.reset();
		}

		PeerConnectionMonitor* TransportMonitor::GetPeerConnectionMonitor() const
		{
			return this->clientMonitor->GetPeerConnectionMonitor(this->transportId);
		}

		void TransportMonitor::OnConnectionStateChange(const std::string& connectionState)
		{
			if (auto* monitor = this->GetPeerConnectionMonitor())
			{
				monitor->SetConnectionState(connectionState);
			}

			this->FireEvent(
			  ClientEventTypes::PEER_CONNECTION_STATE_CHANGED,
			  {
			    { "connectionState", connectionState }
      });
		}

		void TransportMonitor::OnProducerCreated(::mediasoupclient::Producer* producer)
		{
			const auto trackId = TrackIdOf(producer->GetTrack());

			if (auto* monitor = this->GetPeerConnectionMonitor())
			{
				if (auto track = Wrap(producer->GetTrack()))
				{
					monitor->AddMediaTrack(
					  track,
					  {
					    { "producerId", producer->GetId() }
          });
				}
			}
			if (!trackId.empty())
			{
				OutboundTrackContext context;

				context.paused = producer->IsPaused();
				this->clientMonitor->SetOutboundTrackContext(trackId, context);
			}

			this->FireEvent(
			  ClientEventTypes::PRODUCER_ADDED,
			  {
			    { "producerId", producer->GetId() },
          { "trackId",    trackId           }
      });
		}

		void TransportMonitor::OnProducerPaused(::mediasoupclient::Producer* producer)
		{
			const auto trackId = TrackIdOf(producer->GetTrack());
			OutboundTrackContext context;

			context.paused = true;
			this->clientMonitor->SetOutboundTrackContext(trackId, context);
			this->FireEvent(
			  ClientEventTypes::PRODUCER_PAUSED,
			  {
			    { "producerId", producer->GetId() },
          { "trackId",    trackId           }
      });
		}

		void TransportMonitor::OnProducerResumed(::mediasoupclient::Producer* producer)
		{
			const auto trackId = TrackIdOf(producer->GetTrack());
			OutboundTrackContext context;

			context.paused = false;
			this->clientMonitor->SetOutboundTrackContext(trackId, context);
			this->FireEvent(
			  ClientEventTypes::PRODUCER_RESUMED,
			  {
			    { "producerId", producer->GetId() },
          { "trackId",    trackId           }
      });
		}

		void TransportMonitor::OnProducerTrackReplaced(::mediasoupclient::Producer* producer)
		{
			if (auto* monitor = this->GetPeerConnectionMonitor())
			{
				if (auto track = Wrap(producer->GetTrack()))
				{
					monitor->AddMediaTrack(
					  track,
					  {
					    { "producerId", producer->GetId() }
          });
				}
			}
		}

		void TransportMonitor::OnProducerClosed(::mediasoupclient::Producer* producer)
		{
			const auto trackId = TrackIdOf(producer->GetTrack());

			if (auto* monitor = this->GetPeerConnectionMonitor())
			{
				monitor->OnMediaTrackEnded(trackId);
			}

			this->FireEvent(
			  ClientEventTypes::PRODUCER_REMOVED,
			  {
			    { "producerId", producer->GetId() },
          { "trackId",    trackId           }
      });
		}

		void TransportMonitor::OnConsumerCreated(::mediasoupclient::Consumer* consumer)
		{
			const auto trackId = TrackIdOf(consumer->GetTrack());

			// A consumer can be created already paused; declared before its stats appear.
			InboundTrackContext context;

			context.paused = consumer->IsPaused();
			this->clientMonitor->SetInboundTrackContext(trackId, context);

			if (auto* monitor = this->GetPeerConnectionMonitor())
			{
				if (auto track = Wrap(consumer->GetTrack()))
				{
					monitor->AddMediaTrack(
					  track,
					  {
					    { "producerId", consumer->GetProducerId() },
              { "consumerId", consumer->GetId()         }
          });
				}
			}

			this->FireEvent(
			  ClientEventTypes::CONSUMER_ADDED,
			  {
			    { "producerId", consumer->GetProducerId() },
			    { "consumerId", consumer->GetId()         },
			    { "trackId",    trackId                   }
      });
		}

		void TransportMonitor::OnConsumerPaused(::mediasoupclient::Consumer* consumer)
		{
			const auto trackId = TrackIdOf(consumer->GetTrack());
			InboundTrackContext context;

			context.paused = true;
			this->clientMonitor->SetInboundTrackContext(trackId, context);
			this->FireEvent(
			  ClientEventTypes::CONSUMER_PAUSED,
			  {
			    { "producerId", consumer->GetProducerId() },
			    { "consumerId", consumer->GetId()         },
			    { "trackId",    trackId                   }
      });
		}

		void TransportMonitor::OnConsumerResumed(::mediasoupclient::Consumer* consumer)
		{
			const auto trackId = TrackIdOf(consumer->GetTrack());
			InboundTrackContext context;

			context.paused = false;
			this->clientMonitor->SetInboundTrackContext(trackId, context);
			this->FireEvent(
			  ClientEventTypes::CONSUMER_RESUMED,
			  {
			    { "producerId", consumer->GetProducerId() },
			    { "consumerId", consumer->GetId()         },
			    { "trackId",    trackId                   }
      });
		}

		void TransportMonitor::OnConsumerClosed(::mediasoupclient::Consumer* consumer)
		{
			const auto trackId = TrackIdOf(consumer->GetTrack());

			if (auto* monitor = this->GetPeerConnectionMonitor())
			{
				monitor->OnMediaTrackEnded(trackId);
			}

			this->FireEvent(
			  ClientEventTypes::CONSUMER_REMOVED,
			  {
			    { "producerId", consumer->GetProducerId() },
			    { "consumerId", consumer->GetId()         },
			    { "trackId",    trackId                   }
      });
		}

		void TransportMonitor::OnDataProducerCreated(::mediasoupclient::DataProducer* dataProducer)
		{
			this->FireEvent(
			  ClientEventTypes::DATA_PRODUCER_CREATED,
			  {
			    { "dataProducerId", dataProducer->GetId() }
      });
		}

		void TransportMonitor::OnDataProducerClosed(::mediasoupclient::DataProducer* dataProducer)
		{
			this->FireEvent(
			  ClientEventTypes::DATA_PRODUCER_CLOSED,
			  {
			    { "dataProducerId", dataProducer->GetId() }
      });
		}

		void TransportMonitor::OnDataConsumerCreated(::mediasoupclient::DataConsumer* dataConsumer)
		{
			this->FireEvent(
			  ClientEventTypes::DATA_CONSUMER_CREATED,
			  {
			    { "dataProducerId", dataConsumer->GetDataProducerId() },
			    { "dataConsumerId", dataConsumer->GetId()             }
      });
		}

		void TransportMonitor::OnDataConsumerClosed(::mediasoupclient::DataConsumer* dataConsumer)
		{
			this->FireEvent(
			  ClientEventTypes::DATA_CONSUMER_CLOSED,
			  {
			    { "dataProducerId", dataConsumer->GetDataProducerId() },
			    { "dataConsumerId", dataConsumer->GetId()             }
      });
		}

		void TransportMonitor::FireEvent(const char* type, json payload)
		{
			payload["peerConnectionId"] = this->transportId;

			this->clientMonitor->AddEvent(type, payload);
		}

		/* MonitoredSendTransportListener */

		MonitoredSendTransportListener::MonitoredSendTransportListener(
		  TransportMonitor* monitor, ::mediasoupclient::SendTransport::Listener* inner)
		  : monitor(monitor), inner(inner)
		{
		}

		std::future<void> MonitoredSendTransportListener::OnConnect(
		  ::mediasoupclient::Transport* transport, const json& dtlsParameters)
		{
			return this->inner->OnConnect(transport, dtlsParameters);
		}

		void MonitoredSendTransportListener::OnConnectionStateChange(
		  ::mediasoupclient::Transport* transport, const std::string& connectionState)
		{
			this->monitor->OnConnectionStateChange(connectionState);
			this->inner->OnConnectionStateChange(transport, connectionState);
		}

		std::future<std::string> MonitoredSendTransportListener::OnProduce(
		  ::mediasoupclient::SendTransport* transport,
		  const std::string& kind,
		  json rtpParameters,
		  const json& appData)
		{
			return this->inner->OnProduce(transport, kind, std::move(rtpParameters), appData);
		}

		std::future<std::string> MonitoredSendTransportListener::OnProduceData(
		  ::mediasoupclient::SendTransport* transport,
		  const json& sctpStreamParameters,
		  const std::string& label,
		  const std::string& protocol,
		  const json& appData)
		{
			return this->inner->OnProduceData(transport, sctpStreamParameters, label, protocol, appData);
		}

		/* MonitoredRecvTransportListener */

		MonitoredRecvTransportListener::MonitoredRecvTransportListener(
		  TransportMonitor* monitor, ::mediasoupclient::RecvTransport::Listener* inner)
		  : monitor(monitor), inner(inner)
		{
		}

		std::future<void> MonitoredRecvTransportListener::OnConnect(
		  ::mediasoupclient::Transport* transport, const json& dtlsParameters)
		{
			return this->inner->OnConnect(transport, dtlsParameters);
		}

		void MonitoredRecvTransportListener::OnConnectionStateChange(
		  ::mediasoupclient::Transport* transport, const std::string& connectionState)
		{
			this->monitor->OnConnectionStateChange(connectionState);
			this->inner->OnConnectionStateChange(transport, connectionState);
		}
	} // namespace mediasoupclient
} // namespace clientmonitor
