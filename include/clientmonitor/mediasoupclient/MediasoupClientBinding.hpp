#ifndef CM_BINDINGS_MEDIASOUPCLIENT_BINDING_HPP
#define CM_BINDINGS_MEDIASOUPCLIENT_BINDING_HPP

/**
 * Binds libmediasoupclient transports to a ClientMonitor. Built with
 * -DCLIENTMONITOR_MEDIASOUPCLIENT=ON (and the mediasoupclient target available).
 *
 * libmediasoupclient has no observer to subscribe to: the application owns every Listener and
 * every call. So the binding is two pieces, in the library's own style:
 *
 *  - TransportMonitor: one per mediasoup transport. Bind() it once the transport exists, and
 *    tell it about producers and consumers where the application creates, pauses and closes them.
 *  - MonitoredSendTransportListener / MonitoredRecvTransportListener: decorators to hand to
 *    Device::CreateSendTransport() / CreateRecvTransport() in place of your own listener. They
 *    forward everything and report the transport's connection state.
 *
 *   auto transportMonitor = std::make_unique<TransportMonitor>(&clientMonitor);
 *   MonitoredSendTransportListener listener(transportMonitor.get(), &myListener);
 *   auto* sendTransport = device.CreateSendTransport(&listener, ...);
 *   transportMonitor->Bind(sendTransport);
 *   auto* producer = sendTransport->Produce(...);
 *   transportMonitor->OnProducerCreated(producer);
 *
 * What ends up in the samples, as the web client's MediasoupTransportBinding sends it, so the
 * observer can join client stats to the SFU's objects:
 *
 *   peerConnections[].peerConnectionId        the transport id
 *   peerConnections[].attachments.direction   "send" | "recv"
 *   outboundTracks[].attachments              { producerId }
 *   inboundTracks[].attachments               { producerId, consumerId }
 *   dataChannels[].attachments                { dataProducerId } | { dataProducerId, dataConsumerId }
 *
 * plus the PRODUCER_*, CONSUMER_*, DATA_PRODUCER_*, DATA_CONSUMER_*, PEER_CONNECTION_* and
 * MEDIA_TRACK_REMOVED events with the same payload keys.
 */

#include "clientmonitor/ClientMonitor.hpp"

#include <Consumer.hpp>
#include <DataConsumer.hpp>
#include <DataProducer.hpp>
#include <Producer.hpp>
#include <Transport.hpp>

#include <atomic>
#include <future>
#include <map>
#include <string>

namespace observertc::clientmonitor
{
	namespace mediasoupclient
	{
		/** A StatsCollector over mediasoupclient::Transport::GetStats(). */
		class TransportStatsCollector : public StatsCollector
		{
		public:
			explicit TransportStatsCollector(::mediasoupclient::Transport* transport);

			nlohmann::json GetStats() override;
			/**
			 * Called by TransportMonitor::Close(). Does not wait for a GetStats() in flight (that
			 * would deadlock when Close() runs on the thread delivering the stats), so keep the
			 * transport alive for one collecting period after closing the monitor.
			 */
			void Detach();

		private:
			std::atomic<::mediasoupclient::Transport*> transport{ nullptr };
		};

		/** Monitors one mediasoup transport as one peer connection. Port of `MediasoupTransportBinding`. */
		class TransportMonitor
		{
		public:
			/**
			 * `attachments` go on the peer connection entry of every sample, next to the
			 * `direction` ("send" or "recv") Bind() adds from the transport's type.
			 */
			explicit TransportMonitor(
			  ClientMonitor* clientMonitor, const nlohmann::json& attachments = nlohmann::json());
			~TransportMonitor();

		public:
			/**
			 * Starts monitoring; the transport id becomes the peer connection id, as the SFU knows
			 * the WebRtcTransport by it.
			 */
			PeerConnectionMonitor* Bind(::mediasoupclient::Transport* transport);
			/** Stops monitoring. Call before closing the transport. */
			void Close();
			PeerConnectionMonitor* GetPeerConnectionMonitor() const;
			const std::string& GetTransportId() const
			{
				return this->transportId;
			}
			/** "send" or "recv" once bound, empty before. */
			const std::string& GetDirection() const
			{
				return this->direction;
			}

			void OnConnectionStateChange(const std::string& connectionState);

			void OnProducerCreated(::mediasoupclient::Producer* producer);
			void OnProducerPaused(::mediasoupclient::Producer* producer);
			void OnProducerResumed(::mediasoupclient::Producer* producer);
			/** Also after ReplaceTrack(): the new track is monitored from then on. */
			void OnProducerTrackReplaced(::mediasoupclient::Producer* producer);
			void OnProducerClosed(::mediasoupclient::Producer* producer);

			void OnConsumerCreated(::mediasoupclient::Consumer* consumer);
			void OnConsumerPaused(::mediasoupclient::Consumer* consumer);
			void OnConsumerResumed(::mediasoupclient::Consumer* consumer);
			void OnConsumerClosed(::mediasoupclient::Consumer* consumer);
			/**
			 * The server paused or resumed the producer this consumer reads from (mediasoup's
			 * `producerpause` / `producerresume` notifications). Keeps the dry-track detector from
			 * raising on a track whose sender stopped on purpose.
			 */
			void OnConsumerProducerPaused(::mediasoupclient::Consumer* consumer);
			void OnConsumerProducerResumed(::mediasoupclient::Consumer* consumer);

			void OnDataProducerCreated(::mediasoupclient::DataProducer* dataProducer);
			void OnDataProducerClosed(::mediasoupclient::DataProducer* dataProducer);
			void OnDataConsumerCreated(::mediasoupclient::DataConsumer* dataConsumer);
			void OnDataConsumerClosed(::mediasoupclient::DataConsumer* dataConsumer);

		private:
			void FireEvent(const char* type, nlohmann::json payload);
			/** MEDIA_TRACK_REMOVED for `trackId`; `track` (may be null) adds kind, enabled, state. */
			void FireTrackRemoved(
			  const std::string& trackId,
			  ::webrtc::MediaStreamTrackInterface* track,
			  const nlohmann::json& attachments);
			/** Starts monitoring `producer`'s current track, dropping the one it had before. */
			void TrackProducer(::mediasoupclient::Producer* producer);

		private:
			ClientMonitor* clientMonitor{ nullptr };
			nlohmann::json attachments;
			std::string transportId;
			std::string direction;
			std::shared_ptr<TransportStatsCollector> collector;
			// Producer id -> the track id monitored for it. ReplaceTrack() changes the track, not
			// the producer, and a closed producer may no longer name its track.
			std::map<std::string, std::string> producerTracks;
		};

		class MonitoredSendTransportListener : public ::mediasoupclient::SendTransport::Listener
		{
		public:
			MonitoredSendTransportListener(
			  TransportMonitor* monitor, ::mediasoupclient::SendTransport::Listener* inner);

			std::future<void> OnConnect(
			  ::mediasoupclient::Transport* transport, const nlohmann::json& dtlsParameters) override;
			void OnConnectionStateChange(
			  ::mediasoupclient::Transport* transport, const std::string& connectionState) override;
			std::future<std::string> OnProduce(
			  ::mediasoupclient::SendTransport* transport,
			  const std::string& kind,
			  nlohmann::json rtpParameters,
			  const nlohmann::json& appData) override;
			std::future<std::string> OnProduceData(
			  ::mediasoupclient::SendTransport* transport,
			  const nlohmann::json& sctpStreamParameters,
			  const std::string& label,
			  const std::string& protocol,
			  const nlohmann::json& appData) override;

		private:
			TransportMonitor* monitor{ nullptr };
			::mediasoupclient::SendTransport::Listener* inner{ nullptr };
		};

		class MonitoredRecvTransportListener : public ::mediasoupclient::RecvTransport::Listener
		{
		public:
			MonitoredRecvTransportListener(
			  TransportMonitor* monitor, ::mediasoupclient::RecvTransport::Listener* inner);

			std::future<void> OnConnect(
			  ::mediasoupclient::Transport* transport, const nlohmann::json& dtlsParameters) override;
			void OnConnectionStateChange(
			  ::mediasoupclient::Transport* transport, const std::string& connectionState) override;

		private:
			TransportMonitor* monitor{ nullptr };
			::mediasoupclient::RecvTransport::Listener* inner{ nullptr };
		};
	} // namespace mediasoupclient
} // namespace observertc::clientmonitor

#endif
