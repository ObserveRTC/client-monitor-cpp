#ifndef CM_MONITORS_PEER_CONNECTION_MONITOR_HPP
#define CM_MONITORS_PEER_CONNECTION_MONITOR_HPP

#include "clientmonitor/utils/IssueRegistry.hpp"
#include "clientmonitor/utils/SlicedWindow.hpp"
#include "clientmonitor/MediaTrack.hpp"
#include "clientmonitor/StatsCollector.hpp"
#include "clientmonitor/detectors/Detector.hpp"
#include "clientmonitor/monitors/IceMonitors.hpp"
#include "clientmonitor/monitors/MediaMonitors.hpp"
#include "clientmonitor/monitors/RtpMonitors.hpp"
#include "clientmonitor/monitors/TrackMonitors.hpp"
#include "clientmonitor/schema/SchemaTypes.hpp"

#include <nlohmann/json.hpp>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace observertc::clientmonitor
{
	// Fast forward declarations.
	class ClientMonitor;

	/**
	 * Monitors one RTCPeerConnection: every stats object it reports, the tracks it carries, and
	 * the connection-level aggregates the detectors judge. Port of the JS `PeerConnectionMonitor`.
	 *
	 * Created by ClientMonitor::AddPeerConnection(), which owns it.
	 */
	class PeerConnectionMonitor
	{
	public:
		/** The running totals fed into the connection's sliced window, by index. */
		enum WindowTotal : size_t
		{
			// Seconds of RTCP round trip summed over the reports, times 1000.
			TOTAL_RTCP_ROUND_TRIP_TIME_IN_MS = 0,
			// How many RTCP round trip measurements those milliseconds are spread over.
			TOTAL_RTCP_ROUND_TRIP_MEASUREMENTS,
			// The same for the round trip ICE measures with its connectivity checks.
			TOTAL_ICE_ROUND_TRIP_TIME_IN_MS,
			TOTAL_ICE_RESPONSES_RECEIVED,
			WINDOW_TOTALS_COUNT
		};

		/** Connection-level aggregates, recomputed on every collection. */
		struct Metrics
		{
			double sendingAudioBitrate{ 0 };
			double sendingVideoBitrate{ 0 };
			double receivingAudioBitrate{ 0 };
			double receivingVideoBitrate{ 0 };
			double dataChannelSendingBitrate{ 0 };
			double dataChannelReceivingBitrate{ 0 };
			double outboundFractionLost{ 0 };
			double inboundFractionalLost{ 0 };
			std::optional<double> avgInboundFractionLost;
			std::optional<double> avgOutboundFractionLost;
			std::optional<double> avgInboundJitterInMs;
			// [0, 1] conversational reading of the transport (E-model).
			std::optional<double> transportStability;
			// Milliseconds between this collection's newest stats timestamp and the previous one's.
			std::optional<double> deltaTime;
			// Sum of every deltaTime: the connection's own stats clock.
			double statsClockTime{ 0 };

			std::optional<double> totalInboundPacketsLost;
			std::optional<double> totalInboundPacketsReceived;
			std::optional<double> totalOutboundPacketsSent;
			std::optional<double> totalOutboundPacketsReceived;
			std::optional<double> totalOutboundPacketsLost;
			std::optional<double> totalDataChannelBytesSent;
			std::optional<double> totalDataChannelBytesReceived;
			std::optional<double> totalSentAudioBytes;
			std::optional<double> totalSentVideoBytes;
			std::optional<double> totalReceivedAudioBytes;
			std::optional<double> totalReceivedVideoBytes;
			std::optional<double> totalVideoEncodeTimeInMs;
			std::optional<double> totalVideoDecodeTimeInMs;
			std::optional<double> totalAvailableIncomingBitrate;
			std::optional<double> totalAvailableOutgoingBitrate;
			std::optional<double> totalPacketSendDelayInSec;

			std::optional<double> deltaInboundPacketsLost;
			std::optional<double> deltaInboundPacketsReceived;
			std::optional<double> deltaOutboundPacketsSent;
			std::optional<double> deltaOutboundPacketsReceived;
			std::optional<double> deltaOutboundPacketsLost;
			std::optional<double> deltaAudioBytesSent;
			std::optional<double> deltaVideoBytesSent;
			std::optional<double> deltaAudioBytesReceived;
			std::optional<double> deltaVideoBytesReceived;
			std::optional<double> deltaDataChannelBytesReceived;
			std::optional<double> deltaDataChannelBytesSent;
			std::optional<double> deltaPacketSendDelayInSec;
			std::optional<double> deltaVideoPacketsSent;
			std::optional<double> deltaVideoEncodeTimeInMs;
			std::optional<double> deltaVideoDecodeTimeInMs;
			std::optional<double> avgPacketSendDelayInMs;
			std::optional<double> deltaInboundVideoJitterBufferDelayInSec;
			std::optional<double> deltaInboundVideoJitterBufferEmittedCount;
			std::optional<double> avgInboundVideoJitterBufferDelayInMs;

			bool hasInboundMedia{ false };
			bool hasOutboundMedia{ false };
			bool hasInboundVideo{ false };
			// The most limiting reason over the streams that sent this collection.
			std::optional<std::string> qualityLimitationReason;

			std::optional<double> iceRttInSec;
			std::optional<double> ewmaIceRttInSec;
			std::optional<double> rtcpRttInSec;
			std::optional<double> ewmaRtcpRttInSec;
			// Most severe ICE state across the transports.
			std::optional<std::string> iceState;
			std::optional<bool> usingTURN;
			std::optional<bool> usingTCP;
		};

		/** Verdicts the connection-level detectors write. */
		struct Verdicts
		{
			bool uplinkCongested{ false };
			std::optional<double> uplinkVideoCongestionSeverity;
			bool downlinkCongested{ false };
			std::optional<double> downlinkVideoCongestionSeverity;
			bool cpulimited{ false };
			std::optional<bool> stalledRtpSender;
			std::optional<bool> stalledTransportDemux;
		};

	public:
		PeerConnectionMonitor(
		  ClientMonitor* parent,
		  std::string peerConnectionId,
		  std::shared_ptr<StatsCollector> statsCollector,
		  const nlohmann::json& attachments);
		~PeerConnectionMonitor();

		PeerConnectionMonitor(const PeerConnectionMonitor&)            = delete;
		PeerConnectionMonitor& operator=(const PeerConnectionMonitor&) = delete;

	public:
		const std::string& GetPeerConnectionId() const
		{
			return this->peerConnectionId;
		}
		ClientMonitor* GetParent() const
		{
			return this->parent;
		}
		bool IsClosed() const
		{
			return this->closed;
		}

		/** Pulls a report from the StatsCollector, adapts it and accepts it. */
		nlohmann::json Collect();
		/**
		 * Accepts a report the application collected itself (a JSON array of W3C stats). Adapted
		 * first, like a collected one.
		 */
		void Accept(nlohmann::json stats);

		void AddStatsAdapter(std::unique_ptr<StatsAdapter> adapter);
		bool RemoveStatsAdapter(const std::string& name);

		/**
		 * Starts monitoring a track. Matched to its stats by track id; held pending until the first
		 * report naming it arrives.
		 */
		void AddMediaTrack(
		  std::shared_ptr<MediaTrack> track, const nlohmann::json& attachments = nlohmann::json());
		/**
		 * The track's `ended` event: an inbound track is dropped, an outbound one is flagged so its
		 * detectors get a last look before it is dropped at the next collection.
		 */
		void OnMediaTrackEnded(const std::string& trackId);

		/**
		 * Attachments for the data channel whose `dataChannelIdentifier` (the SCTP stream id) is
		 * `dataChannelIdentifier`, merged into it now if its stats are already known and when they
		 * first appear otherwise. This is how a mediasoup data producer or consumer is bound to its
		 * RTCDataChannel stats: `sctpStreamParameters.streamId` is the identifier.
		 */
		void SetDataChannelAttachments(int64_t dataChannelIdentifier, const nlohmann::json& attachments);
		/** Stops attaching to channels that appear later under the identifier (it can be reused). */
		void RemoveDataChannelAttachments(int64_t dataChannelIdentifier);

		/** The RTCPeerConnection's `connectionState`, as reported by the binding. */
		void SetConnectionState(const std::string& state);
		const std::optional<std::string>& GetConnectionState() const
		{
			return this->connectionState;
		}
		std::optional<int64_t> GetConnectingStartedAt() const
		{
			return this->connectingStartedAt;
		}
		std::optional<int64_t> GetConnectedAt() const
		{
			return this->connectedAt;
		}
		void SetIceGatheringState(const std::string& state)
		{
			this->iceGatheringState = state;
		}
		const std::optional<std::string>& GetIceGatheringState() const
		{
			return this->iceGatheringState;
		}

		PeerConnectionSample CreateSample();
		void Close();

	public:
		/* Aggregates. */
		const Metrics& GetMetrics() const
		{
			return this->metrics;
		}
		const Verdicts& GetVerdicts() const
		{
			return this->verdicts;
		}
		Verdicts& GetVerdicts()
		{
			return this->verdicts;
		}
		/** RTCP where available, else ICE. The two span different paths and are never blended. */
		std::optional<double> GetAvgRttInSec() const;
		std::optional<double> GetEwmaRttInSec() const;
		double GetSendingBitrate() const;
		double GetReceivingBitrate() const;
		CalculatedScore& GetCalculatedStabilityScore()
		{
			return this->calculatedStabilityScore;
		}
		std::optional<double> GetScore() const
		{
			return this->calculatedStabilityScore.value;
		}

		Detectors& GetDetectors()
		{
			return this->detectors;
		}
		IssueRegistry& GetIssues()
		{
			return this->issues;
		}
		const SlicedWindow& GetSlicedWindow() const
		{
			return *this->slicedWindow;
		}
		nlohmann::json& GetAttachments()
		{
			return this->attachments;
		}
		nlohmann::json& GetAppData()
		{
			return this->appData;
		}

		/* The graph. Vectors are fresh snapshots; pointers stay valid until the next collection. */
		std::vector<CodecMonitor*> GetCodecs() const;
		std::vector<InboundRtpMonitor*> GetInboundRtps() const;
		std::vector<OutboundRtpMonitor*> GetOutboundRtps() const;
		std::vector<RemoteInboundRtpMonitor*> GetRemoteInboundRtps() const;
		std::vector<RemoteOutboundRtpMonitor*> GetRemoteOutboundRtps() const;
		std::vector<MediaSourceMonitor*> GetMediaSources() const;
		std::vector<MediaPlayoutMonitor*> GetMediaPlayouts() const;
		std::vector<DataChannelMonitor*> GetDataChannels() const;
		std::vector<PeerConnectionTransportMonitor*> GetPeerConnectionTransports() const;
		std::vector<IceTransportMonitor*> GetIceTransports() const;
		std::vector<IceCandidateMonitor*> GetIceCandidates() const;
		std::vector<IceCandidateMonitor*> GetLocalIceCandidates() const;
		std::vector<IceCandidatePairMonitor*> GetIceCandidatePairs() const;
		std::vector<IceCandidatePairMonitor*> GetSelectedIceCandidatePairs() const;
		std::vector<CertificateMonitor*> GetCertificates() const;
		std::vector<InboundTrackMonitor*> GetInboundTracks() const;
		std::vector<OutboundTrackMonitor*> GetOutboundTracks() const;

		CodecMonitor* GetCodecMonitor(const std::string& id) const;
		InboundRtpMonitor* GetInboundRtpMonitor(int64_t ssrc) const;
		OutboundRtpMonitor* GetOutboundRtpMonitor(int64_t ssrc) const;
		RemoteInboundRtpMonitor* GetRemoteInboundRtpMonitor(int64_t ssrc) const;
		RemoteOutboundRtpMonitor* GetRemoteOutboundRtpMonitor(int64_t ssrc) const;
		MediaSourceMonitor* GetMediaSourceMonitor(const std::string& id) const;
		MediaPlayoutMonitor* GetMediaPlayoutMonitor(const std::string& id) const;
		DataChannelMonitor* GetDataChannelMonitor(const std::string& id) const;
		IceTransportMonitor* GetIceTransportMonitor(const std::string& id) const;
		IceCandidateMonitor* GetIceCandidateMonitor(const std::string& id) const;
		IceCandidatePairMonitor* GetIceCandidatePairMonitor(const std::string& id) const;
		CertificateMonitor* GetCertificateMonitor(const std::string& id) const;
		InboundTrackMonitor* GetInboundTrackMonitor(const std::string& trackId) const;
		OutboundTrackMonitor* GetOutboundTrackMonitor(const std::string& trackId) const;
		/** True while any ICE transport of this connection is blocked. */
		bool IsBlockedTransport() const;

	private:
		void AcceptAdaptedStats(const nlohmann::json& stats);
		void UpdateCodec(const nlohmann::json& raw);
		InboundRtpMonitor* UpdateInboundRtp(const nlohmann::json& raw);
		OutboundRtpMonitor* UpdateOutboundRtp(const nlohmann::json& raw);
		RemoteInboundRtpMonitor* UpdateRemoteInboundRtp(const nlohmann::json& raw);
		RemoteOutboundRtpMonitor* UpdateRemoteOutboundRtp(const nlohmann::json& raw);
		void UpdateMediaSource(const nlohmann::json& raw);
		void UpdateMediaPlayout(const nlohmann::json& raw);
		DataChannelMonitor* UpdateDataChannel(const nlohmann::json& raw);
		void UpdatePeerConnectionTransport(const nlohmann::json& raw);
		void UpdateIceTransport(const nlohmann::json& raw);
		void UpdateIceCandidate(const nlohmann::json& raw, IceCandidateMonitor::Direction direction);
		void UpdateIceCandidatePair(const nlohmann::json& raw);
		void UpdateCertificate(const nlohmann::json& raw);
		void CreateInboundTrackMonitor(
		  const std::shared_ptr<MediaTrack>& track,
		  InboundRtpMonitor* inboundRtp,
		  const nlohmann::json& attachments);
		void CreateOutboundTrackMonitor(
		  const std::shared_ptr<MediaTrack>& track,
		  MediaSourceMonitor* mediaSource,
		  const nlohmann::json& attachments);
		void UpdatePeerConnectionLimitationReason(const std::optional<std::string>& current);
		void UpdateTransportQualityAverages();
		void RefreshTransportStability();
		void FeedSlicedWindow();
		std::optional<std::string> MostSevereIceState() const;
		bool HasLiveTrack(const std::optional<std::string>& trackIdentifier) const;
		void CheckVisited();

	private:
		struct PendingTrack
		{
			std::shared_ptr<MediaTrack> track;
			nlohmann::json attachments;
		};

		ClientMonitor* parent{ nullptr };
		std::string peerConnectionId;
		std::shared_ptr<StatsCollector> statsCollector;
		nlohmann::json attachments;
		nlohmann::json appData = nlohmann::json::object();
		bool closed{ false };
		std::vector<std::unique_ptr<StatsAdapter>> statsAdapters;
		IssueRegistry issues;
		std::unique_ptr<SlicedWindow> slicedWindow;
		Detectors detectors;

		Metrics metrics;
		Verdicts verdicts;
		CalculatedScore calculatedStabilityScore;
		std::optional<double> previousNewestTimestamp;
		std::optional<std::string> connectionState;
		std::optional<int64_t> connectingStartedAt;
		std::optional<int64_t> connectedAt;
		std::optional<std::string> iceGatheringState;

		// Tracks are declared after the monitors they reference so they are destroyed first.
		std::map<std::string, std::unique_ptr<CodecMonitor>> codecs;
		std::map<int64_t, std::unique_ptr<InboundRtpMonitor>> inboundRtps;
		std::map<int64_t, std::unique_ptr<OutboundRtpMonitor>> outboundRtps;
		std::map<int64_t, std::unique_ptr<RemoteInboundRtpMonitor>> remoteInboundRtps;
		std::map<int64_t, std::unique_ptr<RemoteOutboundRtpMonitor>> remoteOutboundRtps;
		std::map<std::string, std::unique_ptr<MediaSourceMonitor>> mediaSources;
		std::map<std::string, std::unique_ptr<MediaPlayoutMonitor>> mediaPlayouts;
		std::map<std::string, std::unique_ptr<DataChannelMonitor>> dataChannels;
		std::map<std::string, std::unique_ptr<PeerConnectionTransportMonitor>> peerConnectionTransports;
		std::map<std::string, std::unique_ptr<IceTransportMonitor>> iceTransports;
		std::map<std::string, std::unique_ptr<IceCandidateMonitor>> iceCandidates;
		std::map<std::string, std::unique_ptr<IceCandidatePairMonitor>> iceCandidatePairs;
		std::map<std::string, std::unique_ptr<CertificateMonitor>> certificates;
		std::map<std::string, PendingTrack> pendingTracks;
		std::map<int64_t, nlohmann::json> dataChannelAttachments;
		std::map<std::string, std::unique_ptr<InboundTrackMonitor>> inboundTracks;
		std::map<std::string, std::unique_ptr<OutboundTrackMonitor>> outboundTracks;
	};
} // namespace observertc::clientmonitor

#endif
