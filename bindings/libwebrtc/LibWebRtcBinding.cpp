#define CM_CLASS "LibWebRtcBinding"

#include "clientmonitor/libwebrtc/LibWebRtcBinding.hpp"
#include "clientmonitor/utils/Utils.hpp"
#include "clientmonitor/Logger.hpp"
#include "clientmonitor/schema/ClientEventTypes.hpp"

#include <rtc_base/ref_counted_object.h>

#include <future>

using json = nlohmann::json;

namespace observertc::clientmonitor
{
	namespace libwebrtc
	{
		namespace
		{
			class StatsCallback : public webrtc::RTCStatsCollectorCallback
			{
			public:
				void OnStatsDelivered(const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report) override
				{
					// RtpReceiver stats JSON is sometimes empty (as libmediasoupclient notes).
					const auto text = report ? report->ToJson() : std::string();

					try
					{
						this->promise.set_value(text.empty() ? json::array() : json::parse(text));
					}
					catch (...)
					{
						this->promise.set_value(json::array());
					}
				}

				std::future<json> GetFuture()
				{
					return this->promise.get_future();
				}

			private:
				std::promise<json> promise;
			};
		} // namespace

		/* PeerConnectionStatsCollector */

		PeerConnectionStatsCollector::PeerConnectionStatsCollector(
		  webrtc::scoped_refptr<webrtc::PeerConnectionInterface> peerConnection,
		  std::chrono::milliseconds timeout)
		  : peerConnection(std::move(peerConnection)), timeout(timeout)
		{
		}

		json PeerConnectionStatsCollector::GetStats()
		{
			if (!this->peerConnection)
			{
				return json::array();
			}

			webrtc::scoped_refptr<StatsCallback> callback(new webrtc::RefCountedObject<StatsCallback>());
			auto future = callback->GetFuture();

			this->peerConnection->GetStats(callback.get());

			if (future.wait_for(this->timeout) != std::future_status::ready)
			{
				CM_WARN("timed out waiting for stats");

				return json::array();
			}

			return future.get();
		}

		/* WebRtcMediaTrack */

		WebRtcMediaTrack::WebRtcMediaTrack(
		  webrtc::scoped_refptr<webrtc::MediaStreamTrackInterface> track, std::string label)
		  : track(std::move(track)), label(std::move(label))
		{
		}

		std::string WebRtcMediaTrack::GetId() const
		{
			return this->track->id();
		}

		std::string WebRtcMediaTrack::GetKind() const
		{
			return this->track->kind();
		}

		MediaTrack::ReadyState WebRtcMediaTrack::GetReadyState() const
		{
			return this->track->state() == webrtc::MediaStreamTrackInterface::kEnded ? ReadyState::ENDED
			                                                                         : ReadyState::LIVE;
		}

		std::string WebRtcMediaTrack::GetLabel() const
		{
			return this->label;
		}

		bool WebRtcMediaTrack::IsEnabled() const
		{
			return this->track->enabled();
		}

		/* MonitoredPeerConnectionObserver */

		MonitoredPeerConnectionObserver::MonitoredPeerConnectionObserver(
		  ClientMonitor* clientMonitor,
		  webrtc::PeerConnectionObserver* inner,
		  std::string peerConnectionId,
		  const json& attachments)
		  : clientMonitor(clientMonitor),
		    inner(inner),
		    peerConnectionId(peerConnectionId.empty() ? utils::CreateUuid() : std::move(peerConnectionId)),
		    attachments(attachments)
		{
		}

		PeerConnectionMonitor* MonitoredPeerConnectionObserver::Bind(
		  webrtc::scoped_refptr<webrtc::PeerConnectionInterface> peerConnection)
		{
			this->peerConnection = peerConnection;

			auto* monitor = this->clientMonitor->AddPeerConnection(
			  std::make_shared<PeerConnectionStatsCollector>(peerConnection),
			  this->peerConnectionId,
			  this->attachments);

			if (monitor)
			{
				monitor->SetIceGatheringState(ToString(peerConnection->ice_gathering_state()));
				this->FireEvent(
				  ClientEventTypes::PEER_CONNECTION_OPENED,
				  {
				    { "iceConnectionState", ToString(peerConnection->ice_connection_state()) },
				    { "iceGatheringState",  ToString(peerConnection->ice_gathering_state())  },
				    { "signalingState",     ToString(peerConnection->signaling_state())      }
        });
			}

			return monitor;
		}

		void MonitoredPeerConnectionObserver::AddLocalTrack(
		  webrtc::scoped_refptr<webrtc::MediaStreamTrackInterface> track, const json& attachments)
		{
			if (auto* monitor = this->GetPeerConnectionMonitor())
			{
				monitor->AddMediaTrack(std::make_shared<WebRtcMediaTrack>(std::move(track)), attachments);
			}
		}

		void MonitoredPeerConnectionObserver::OnTrackEnded(const std::string& trackId)
		{
			if (auto* monitor = this->GetPeerConnectionMonitor())
			{
				monitor->OnMediaTrackEnded(trackId);
			}

			this->FireEvent(
			  ClientEventTypes::MEDIA_TRACK_REMOVED,
			  {
			    { "trackId", trackId }
      });
		}

		PeerConnectionMonitor* MonitoredPeerConnectionObserver::GetPeerConnectionMonitor() const
		{
			return this->clientMonitor->GetPeerConnectionMonitor(this->peerConnectionId);
		}

		void MonitoredPeerConnectionObserver::OnSignalingChange(
		  webrtc::PeerConnectionInterface::SignalingState newState)
		{
			this->FireEvent(
			  ClientEventTypes::SIGNALING_STATE_CHANGE,
			  {
			    { "signalingState", ToString(newState) }
      });

			if (this->inner)
			{
				this->inner->OnSignalingChange(newState);
			}
		}

		void MonitoredPeerConnectionObserver::OnDataChannel(
		  webrtc::scoped_refptr<webrtc::DataChannelInterface> dataChannel)
		{
			if (dataChannel)
			{
				this->FireEvent(
				  ClientEventTypes::DATA_CHANNEL_OPEN,
				  {
				    { "label",         dataChannel->label() },
            { "dataChannelId", dataChannel->id()    }
        });
			}
			if (this->inner)
			{
				this->inner->OnDataChannel(std::move(dataChannel));
			}
		}

		void MonitoredPeerConnectionObserver::OnRenegotiationNeeded()
		{
			if (this->inner)
			{
				this->inner->OnRenegotiationNeeded();
			}
		}

		void MonitoredPeerConnectionObserver::OnNegotiationNeededEvent(uint32_t eventId)
		{
			this->FireEvent(ClientEventTypes::NEGOTIATION_NEEDED, json::object());

			if (this->inner)
			{
				this->inner->OnNegotiationNeededEvent(eventId);
			}
		}

		void MonitoredPeerConnectionObserver::OnIceConnectionChange(
		  webrtc::PeerConnectionInterface::IceConnectionState newState)
		{
			this->FireEvent(
			  ClientEventTypes::ICE_CONNECTION_STATE_CHANGED,
			  {
			    { "iceConnectionState", ToString(newState) }
      });

			if (this->inner)
			{
				this->inner->OnIceConnectionChange(newState);
			}
		}

		void MonitoredPeerConnectionObserver::OnConnectionChange(
		  webrtc::PeerConnectionInterface::PeerConnectionState newState)
		{
			const std::string state = ToString(newState);

			if (auto* monitor = this->GetPeerConnectionMonitor())
			{
				monitor->SetConnectionState(state);
			}

			this->FireEvent(
			  ClientEventTypes::PEER_CONNECTION_STATE_CHANGED,
			  {
			    { "connectionState", state }
      });

			if (newState == webrtc::PeerConnectionInterface::PeerConnectionState::kClosed)
			{
				this->FireEvent(ClientEventTypes::PEER_CONNECTION_CLOSED, json::object());
				this->clientMonitor->RemovePeerConnection(this->peerConnectionId);
			}
			if (this->inner)
			{
				this->inner->OnConnectionChange(newState);
			}
		}

		void MonitoredPeerConnectionObserver::OnIceGatheringChange(
		  webrtc::PeerConnectionInterface::IceGatheringState newState)
		{
			if (auto* monitor = this->GetPeerConnectionMonitor())
			{
				monitor->SetIceGatheringState(ToString(newState));
			}

			this->FireEvent(
			  ClientEventTypes::ICE_GATHERING_STATE_CHANGED,
			  {
			    { "iceGatheringState", ToString(newState) }
      });

			if (this->inner)
			{
				this->inner->OnIceGatheringChange(newState);
			}
		}

		void MonitoredPeerConnectionObserver::OnIceCandidate(const webrtc::IceCandidateInterface* candidate)
		{
			if (candidate)
			{
				std::string sdp;

				candidate->ToString(&sdp);

				this->FireEvent(
				  ClientEventTypes::ICE_CANDIDATE,
				  {
				    { "candidate",     sdp                          },
				    { "sdpMid",        candidate->sdp_mid()         },
				    { "sdpMLineIndex", candidate->sdp_mline_index() }
        });
			}
			if (this->inner)
			{
				this->inner->OnIceCandidate(candidate);
			}
		}

		void MonitoredPeerConnectionObserver::OnIceCandidateError(
		  const std::string& address,
		  int port,
		  const std::string& url,
		  int errorCode,
		  const std::string& errorText)
		{
			this->FireEvent(
			  ClientEventTypes::ICE_CANDIDATE_ERROR,
			  {
			    { "address",   address   },
			    { "port",      port      },
			    { "url",       url       },
			    { "errorCode", errorCode },
			    { "errorText", errorText }
      });

			if (this->inner)
			{
				this->inner->OnIceCandidateError(address, port, url, errorCode, errorText);
			}
		}

		void MonitoredPeerConnectionObserver::OnIceConnectionReceivingChange(bool receiving)
		{
			if (this->inner)
			{
				this->inner->OnIceConnectionReceivingChange(receiving);
			}
		}

		void MonitoredPeerConnectionObserver::OnTrack(
		  webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver)
		{
			auto receiver = transceiver ? transceiver->receiver() : nullptr;
			auto track    = receiver ? receiver->track() : nullptr;

			if (track)
			{
				if (auto* monitor = this->GetPeerConnectionMonitor())
				{
					monitor->AddMediaTrack(std::make_shared<WebRtcMediaTrack>(track));
				}

				this->FireEvent(
				  ClientEventTypes::MEDIA_TRACK_ADDED,
				  {
				    { "trackId", track->id()   },
            { "kind",    track->kind() }
        });
			}
			if (this->inner)
			{
				this->inner->OnTrack(std::move(transceiver));
			}
		}

		void MonitoredPeerConnectionObserver::OnRemoveTrack(
		  webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver)
		{
			auto track = receiver ? receiver->track() : nullptr;

			if (track)
			{
				this->OnTrackEnded(track->id());
			}
			if (this->inner)
			{
				this->inner->OnRemoveTrack(std::move(receiver));
			}
		}

		void MonitoredPeerConnectionObserver::FireEvent(const char* type, json payload)
		{
			payload["peerConnectionId"] = this->peerConnectionId;

			this->clientMonitor->AddEvent(type, payload);
		}

		/* Enum names. */

		const char* ToString(webrtc::PeerConnectionInterface::PeerConnectionState state)
		{
			using State = webrtc::PeerConnectionInterface::PeerConnectionState;

			switch (state)
			{
				case State::kNew:
					return "new";
				case State::kConnecting:
					return "connecting";
				case State::kConnected:
					return "connected";
				case State::kDisconnected:
					return "disconnected";
				case State::kFailed:
					return "failed";
				case State::kClosed:
					return "closed";
			}

			return "unknown";
		}

		const char* ToString(webrtc::PeerConnectionInterface::IceConnectionState state)
		{
			using State = webrtc::PeerConnectionInterface::IceConnectionState;

			switch (state)
			{
				case State::kIceConnectionNew:
					return "new";
				case State::kIceConnectionChecking:
					return "checking";
				case State::kIceConnectionConnected:
					return "connected";
				case State::kIceConnectionCompleted:
					return "completed";
				case State::kIceConnectionFailed:
					return "failed";
				case State::kIceConnectionDisconnected:
					return "disconnected";
				case State::kIceConnectionClosed:
					return "closed";
				default:
					return "unknown";
			}
		}

		const char* ToString(webrtc::PeerConnectionInterface::IceGatheringState state)
		{
			using State = webrtc::PeerConnectionInterface::IceGatheringState;

			switch (state)
			{
				case State::kIceGatheringNew:
					return "new";
				case State::kIceGatheringGathering:
					return "gathering";
				case State::kIceGatheringComplete:
					return "complete";
			}

			return "unknown";
		}

		const char* ToString(webrtc::PeerConnectionInterface::SignalingState state)
		{
			using State = webrtc::PeerConnectionInterface::SignalingState;

			switch (state)
			{
				case State::kStable:
					return "stable";
				case State::kHaveLocalOffer:
					return "have-local-offer";
				case State::kHaveLocalPrAnswer:
					return "have-local-pranswer";
				case State::kHaveRemoteOffer:
					return "have-remote-offer";
				case State::kHaveRemotePrAnswer:
					return "have-remote-pranswer";
				case State::kClosed:
					return "closed";
			}

			return "unknown";
		}
	} // namespace libwebrtc
} // namespace observertc::clientmonitor
