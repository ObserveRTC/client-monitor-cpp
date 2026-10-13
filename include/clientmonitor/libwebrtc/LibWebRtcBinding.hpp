#ifndef CM_BINDINGS_LIBWEBRTC_BINDING_HPP
#define CM_BINDINGS_LIBWEBRTC_BINDING_HPP

/**
 * Binds libwebrtc objects to a ClientMonitor. Built with -DCLIENTMONITOR_LIBWEBRTC=ON.
 *
 * Written against the libwebrtc API libmediasoupclient 3.5 builds with (webrtc::scoped_refptr,
 * webrtc::RefCountedObject). Older milestones spell these rtc::; adjust the aliases below.
 */

#include "clientmonitor/ClientMonitor.hpp"
#include "clientmonitor/MediaTrack.hpp"
#include "clientmonitor/StatsCollector.hpp"

#include <api/media_stream_interface.h>
#include <api/peer_connection_interface.h>
#include <api/scoped_refptr.h>
#include <api/stats/rtc_stats_collector_callback.h>
#include <api/stats/rtc_stats_report.h>

#include <chrono>
#include <memory>
#include <string>

namespace observertc::clientmonitor
{
	namespace libwebrtc
	{
		/**
		 * A StatsCollector over a webrtc::PeerConnectionInterface: GetStats() asks the peer
		 * connection for a report and waits for it, up to `timeout`.
		 *
		 * Must not be called on the peer connection's signaling thread, which delivers the report.
		 * The monitor's own timer thread is fine.
		 */
		class PeerConnectionStatsCollector : public StatsCollector
		{
		public:
			explicit PeerConnectionStatsCollector(
			  webrtc::scoped_refptr<webrtc::PeerConnectionInterface> peerConnection,
			  std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

			nlohmann::json GetStats() override;

		private:
			webrtc::scoped_refptr<webrtc::PeerConnectionInterface> peerConnection;
			std::chrono::milliseconds timeout;
		};

		/** A MediaTrack over a webrtc::MediaStreamTrackInterface. */
		class WebRtcMediaTrack : public MediaTrack
		{
		public:
			explicit WebRtcMediaTrack(
			  webrtc::scoped_refptr<webrtc::MediaStreamTrackInterface> track, std::string label = "");

			std::string GetId() const override;
			std::string GetKind() const override;
			ReadyState GetReadyState() const override;
			std::string GetLabel() const override;
			bool IsEnabled() const override;

			webrtc::MediaStreamTrackInterface* GetWebRtcTrack() const
			{
				return this->track.get();
			}

		private:
			webrtc::scoped_refptr<webrtc::MediaStreamTrackInterface> track;
			std::string label;
		};

		/**
		 * A webrtc::PeerConnectionObserver decorator: hand it to CreatePeerConnectionOrError() in
		 * place of your own observer. Every callback is forwarded to `inner` unchanged, and the
		 * monitor is told what the JS RtcPeerConnectionBinding tells it: connection and gathering
		 * state, ICE candidates and errors, negotiation, signalling, and remote tracks.
		 *
		 * Call Bind() once the peer connection exists, so stats can be collected from it.
		 */
		class MonitoredPeerConnectionObserver : public webrtc::PeerConnectionObserver
		{
		public:
			MonitoredPeerConnectionObserver(
			  ClientMonitor* clientMonitor,
			  webrtc::PeerConnectionObserver* inner,
			  std::string peerConnectionId      = "",
			  const nlohmann::json& attachments = nlohmann::json());

		public:
			/** Creates the PeerConnectionMonitor over `peerConnection`. Returns it. */
			PeerConnectionMonitor* Bind(webrtc::scoped_refptr<webrtc::PeerConnectionInterface> peerConnection);
			/** Monitors a local track the application added to the peer connection. */
			void AddLocalTrack(
			  webrtc::scoped_refptr<webrtc::MediaStreamTrackInterface> track,
			  const nlohmann::json& attachments = nlohmann::json());
			/** Tells the monitor a track ended; libwebrtc has no `ended` event to observe. */
			void OnTrackEnded(const std::string& trackId);
			PeerConnectionMonitor* GetPeerConnectionMonitor() const;

			/* Methods inherited from webrtc::PeerConnectionObserver. */
		public:
			void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState newState) override;
			void OnDataChannel(webrtc::scoped_refptr<webrtc::DataChannelInterface> dataChannel) override;
			void OnRenegotiationNeeded() override;
			void OnNegotiationNeededEvent(uint32_t eventId) override;
			void OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState newState) override;
			void OnConnectionChange(webrtc::PeerConnectionInterface::PeerConnectionState newState) override;
			void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState newState) override;
			void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override;
			void OnIceCandidateError(
			  const std::string& address,
			  int port,
			  const std::string& url,
			  int errorCode,
			  const std::string& errorText) override;
			void OnIceConnectionReceivingChange(bool receiving) override;
			void OnTrack(webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) override;
			void OnRemoveTrack(webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) override;

		private:
			void FireEvent(const char* type, nlohmann::json payload);

		private:
			ClientMonitor* clientMonitor{ nullptr };
			webrtc::PeerConnectionObserver* inner{ nullptr };
			std::string peerConnectionId;
			nlohmann::json attachments;
			webrtc::scoped_refptr<webrtc::PeerConnectionInterface> peerConnection;
		};

		/** The W3C strings for libwebrtc's enums, as the stats and events carry them. */
		const char* ToString(webrtc::PeerConnectionInterface::PeerConnectionState state);
		const char* ToString(webrtc::PeerConnectionInterface::IceConnectionState state);
		const char* ToString(webrtc::PeerConnectionInterface::IceGatheringState state);
		const char* ToString(webrtc::PeerConnectionInterface::SignalingState state);
	} // namespace libwebrtc
} // namespace observertc::clientmonitor

#endif
