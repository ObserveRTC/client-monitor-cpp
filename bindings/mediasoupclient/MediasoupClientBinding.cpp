#define CM_CLASS "MediasoupClientBinding"

#include "clientmonitor/mediasoupclient/MediasoupClientBinding.hpp"
#include "clientmonitor/Logger.hpp"
#include "clientmonitor/libwebrtc/LibWebRtcBinding.hpp"
#include "clientmonitor/schema/ClientEventTypes.hpp"

#include <optional>

using json = nlohmann::json;

namespace observertc::clientmonitor
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

			/** The SCTP stream id: what the RTCDataChannel stats call `dataChannelIdentifier`. */
			std::optional<int64_t> StreamIdOf(const json& sctpStreamParameters)
			{
				auto it = sctpStreamParameters.find("streamId");

				if (it == sctpStreamParameters.end() || !it->is_number_integer())
				{
					return std::nullopt;
				}

				return it->get<int64_t>();
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

			// What the web client's monitor attaches from transport.direction. The application's
			// own attachments win on a clash.
			if (dynamic_cast<::mediasoupclient::SendTransport*>(transport))
			{
				this->direction = "send";
			}
			else if (dynamic_cast<::mediasoupclient::RecvTransport*>(transport))
			{
				this->direction = "recv";
			}

			json attachments = json::object();

			if (!this->direction.empty())
			{
				attachments["direction"] = this->direction;
			}
			if (this->attachments.is_object())
			{
				attachments.merge_patch(this->attachments);
			}

			auto* monitor =
			  this->clientMonitor->AddPeerConnection(this->collector, this->transportId, attachments);

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

			std::lock_guard<std::recursive_mutex> lock(this->clientMonitor->GetMutex());

			this->producerTracks.clear();
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

		void TransportMonitor::TrackProducer(::mediasoupclient::Producer* producer)
		{
			// Everything that asks the track (IsPaused() is !track->enabled(), proxied to the
			// signaling thread) runs before the monitor's lock is taken: the signaling thread may be
			// waiting for that lock to report a state change.
			const auto producerId = producer->GetId();
			const auto trackId    = TrackIdOf(producer->GetTrack());
			const bool paused     = producer->IsPaused();
			auto track            = Wrap(producer->GetTrack());
			std::string endedTrackId;

			{
				std::lock_guard<std::recursive_mutex> lock(this->clientMonitor->GetMutex());
				auto previous = this->producerTracks.find(producerId);

				if (previous != this->producerTracks.end())
				{
					if (previous->second == trackId)
					{
						return;
					}

					endedTrackId = previous->second;
					this->producerTracks.erase(previous);
				}
				if (!trackId.empty())
				{
					this->producerTracks[producerId] = trackId;
				}
			}

			auto* monitor = this->GetPeerConnectionMonitor();

			if (monitor && !endedTrackId.empty())
			{
				monitor->OnMediaTrackEnded(endedTrackId);
			}
			if (trackId.empty())
			{
				return;
			}
			if (monitor && track)
			{
				monitor->AddMediaTrack(
				  track,
				  {
				    { "producerId", producerId }
        });
			}

			OutboundTrackContext context;

			context.paused = paused;
			this->clientMonitor->SetOutboundTrackContext(trackId, context);
		}

		void TransportMonitor::OnProducerCreated(::mediasoupclient::Producer* producer)
		{
			this->TrackProducer(producer);
			this->FireEvent(
			  ClientEventTypes::PRODUCER_ADDED,
			  {
			    { "producerId", producer->GetId()               },
          { "trackId",    TrackIdOf(producer->GetTrack()) }
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
			// The track id changes, the producer id does not: the new track carries the same
			// producerId attachment, and the old one is dropped.
			this->TrackProducer(producer);
		}

		void TransportMonitor::OnProducerClosed(::mediasoupclient::Producer* producer)
		{
			const auto producerId = producer->GetId();
			std::string trackId;

			{
				std::lock_guard<std::recursive_mutex> lock(this->clientMonitor->GetMutex());
				auto it = this->producerTracks.find(producerId);

				if (it != this->producerTracks.end())
				{
					trackId = it->second;
					this->producerTracks.erase(it);
				}
			}
			if (trackId.empty())
			{
				trackId = TrackIdOf(producer->GetTrack());
			}
			if (auto* monitor = this->GetPeerConnectionMonitor())
			{
				monitor->OnMediaTrackEnded(trackId);
			}
			if (!trackId.empty())
			{
				this->FireTrackRemoved(
				  trackId,
				  producer->GetTrack(),
				  {
				    { "producerId", producerId }
        });
			}

			this->FireEvent(
			  ClientEventTypes::PRODUCER_REMOVED,
			  {
			    { "producerId", producerId },
          { "trackId",    trackId    }
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

		void TransportMonitor::OnConsumerProducerPaused(::mediasoupclient::Consumer* consumer)
		{
			InboundTrackContext context;

			context.remoteOutboundTrackPaused = true;
			this->clientMonitor->SetInboundTrackContext(TrackIdOf(consumer->GetTrack()), context);
		}

		void TransportMonitor::OnConsumerProducerResumed(::mediasoupclient::Consumer* consumer)
		{
			InboundTrackContext context;

			context.remoteOutboundTrackPaused = false;
			this->clientMonitor->SetInboundTrackContext(TrackIdOf(consumer->GetTrack()), context);
		}

		void TransportMonitor::OnConsumerClosed(::mediasoupclient::Consumer* consumer)
		{
			const auto trackId = TrackIdOf(consumer->GetTrack());
			const json attachments{
				{ "producerId", consumer->GetProducerId() },
        { "consumerId", consumer->GetId()         }
			};

			if (auto* monitor = this->GetPeerConnectionMonitor())
			{
				monitor->OnMediaTrackEnded(trackId);
			}
			if (!trackId.empty())
			{
				this->FireTrackRemoved(trackId, consumer->GetTrack(), attachments);
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
			if (auto streamId = StreamIdOf(dataProducer->GetSctpStreamParameters()))
			{
				if (auto* monitor = this->GetPeerConnectionMonitor())
				{
					monitor->SetDataChannelAttachments(
					  *streamId,
					  {
					    { "dataProducerId", dataProducer->GetId() }
          });
				}
			}

			this->FireEvent(
			  ClientEventTypes::DATA_PRODUCER_CREATED,
			  {
			    { "dataProducerId", dataProducer->GetId() }
      });
		}

		void TransportMonitor::OnDataProducerClosed(::mediasoupclient::DataProducer* dataProducer)
		{
			if (auto streamId = StreamIdOf(dataProducer->GetSctpStreamParameters()))
			{
				if (auto* monitor = this->GetPeerConnectionMonitor())
				{
					monitor->RemoveDataChannelAttachments(*streamId);
				}
			}

			this->FireEvent(
			  ClientEventTypes::DATA_PRODUCER_CLOSED,
			  {
			    { "dataProducerId", dataProducer->GetId() }
      });
		}

		void TransportMonitor::OnDataConsumerCreated(::mediasoupclient::DataConsumer* dataConsumer)
		{
			if (auto streamId = StreamIdOf(dataConsumer->GetSctpStreamParameters()))
			{
				if (auto* monitor = this->GetPeerConnectionMonitor())
				{
					monitor->SetDataChannelAttachments(
					  *streamId,
					  {
					    { "dataProducerId", dataConsumer->GetDataProducerId() },
					    { "dataConsumerId", dataConsumer->GetId()             }
          });
				}
			}

			this->FireEvent(
			  ClientEventTypes::DATA_CONSUMER_CREATED,
			  {
			    { "dataProducerId", dataConsumer->GetDataProducerId() },
			    { "dataConsumerId", dataConsumer->GetId()             }
      });
		}

		void TransportMonitor::OnDataConsumerClosed(::mediasoupclient::DataConsumer* dataConsumer)
		{
			if (auto streamId = StreamIdOf(dataConsumer->GetSctpStreamParameters()))
			{
				if (auto* monitor = this->GetPeerConnectionMonitor())
				{
					monitor->RemoveDataChannelAttachments(*streamId);
				}
			}

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

		void TransportMonitor::FireTrackRemoved(
		  const std::string& trackId, ::webrtc::MediaStreamTrackInterface* track, const json& attachments)
		{
			// The payload of the web client's MEDIA_TRACK_REMOVED, attachments spread in. Native
			// tracks have no label, mute state or content hint to report.
			json payload{
				{ "trackId", trackId }
			};

			if (track)
			{
				payload["kind"]    = track->kind();
				payload["enabled"] = track->enabled();
				payload["readyState"] =
				  track->state() == ::webrtc::MediaStreamTrackInterface::kEnded ? "ended" : "live";
			}

			payload.merge_patch(attachments);
			this->FireEvent(ClientEventTypes::MEDIA_TRACK_REMOVED, std::move(payload));
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
} // namespace observertc::clientmonitor
