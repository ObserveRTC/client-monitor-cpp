#ifndef CM_MONITORS_RTP_MONITORS_HPP
#define CM_MONITORS_RTP_MONITORS_HPP

#include "clientmonitor/monitors/StatsMonitor.hpp"
#include "clientmonitor/schema/SchemaTypes.hpp"

#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace clientmonitor
{
	// Fast forward declarations.
	class CodecMonitor;
	class IceCandidatePairMonitor;
	class IceTransportMonitor;
	class InboundTrackMonitor;
	class MediaPlayoutMonitor;
	class MediaSourceMonitor;
	class OutboundTrackMonitor;
	class RemoteInboundRtpMonitor;
	class RemoteOutboundRtpMonitor;

	/**
	 * One received RTP stream (`inbound-rtp`). Port of the JS `InboundRtpMonitor`.
	 */
	class InboundRtpMonitor : public StatsMonitor<InboundRtpStats>
	{
	public:
		struct Metrics
		{
			std::optional<double> bitrate;
			std::optional<double> packetRate;
			std::optional<double> avgFramesPerSec;
			std::optional<double> fpsVolatility;
			std::optional<double> ewmaFps;
			std::optional<double> bitPerPixel;
			std::optional<double> receivingAudioSamples;
			std::optional<double> totalFractionLost;

			std::optional<double> deltaPacketsLost;
			std::optional<double> deltaPacketsReceived;
			std::optional<double> deltaBytesReceived;
			std::optional<double> deltaJitterBufferDelay;
			std::optional<double> deltaCorruptionProbability;
			std::optional<double> deltaFractionLost;
			std::optional<double> deltaFramesDecoded;
			std::optional<double> deltaFramesReceived;
			std::optional<double> deltaFramesRendered;
			std::optional<double> deltaQpSum;
			std::optional<double> avgQpPerFrame;
			// avgQpPerFrame as a fraction of the codec's quantizer scale.
			std::optional<double> normalizedQp;

			// Audio concealment and jitter buffer.
			std::optional<double> deltaTotalSamplesReceived;
			std::optional<double> deltaConcealedSamples;
			std::optional<double> deltaSilentConcealedSamples;
			std::optional<double> deltaConcealmentEvents;
			std::optional<double> deltaInsertedSamplesForDeceleration;
			std::optional<double> deltaRemovedSamplesForAcceleration;
			std::optional<double> deltaPacketsDiscarded;
			std::optional<double> deltaJitterBufferEmittedCount;
			std::optional<double> deltaJitterBufferTargetDelay;
			std::optional<double> nonSilentConcealedRatio;
			std::optional<double> deltaInterruptionCount;
			std::optional<double> deltaTotalInterruptionDurationInMs;
			std::optional<double> concealmentEventRate;
			std::optional<double> timeStretchRate;
			std::optional<double> avgJitterBufferDelayInMs;
			std::optional<double> jitterBufferTargetDelayInMs;
			std::optional<double> discardRate;

			// Video decode cost and recovery pressure.
			std::optional<double> deltaFramesDropped;
			std::optional<double> deltaKeyFramesDecoded;
			std::optional<double> deltaTotalDecodeTime;
			std::optional<double> deltaTotalFreezesDuration;
			std::optional<double> deltaFreezeCount;
			std::optional<double> frozenTimeRatio;
			std::optional<double> deltaPauseCount;
			std::optional<double> deltaTotalPausesDuration;
			std::optional<double> pausedTimeRatio;
			std::optional<double> deltaTotalInterFrameDelay;
			std::optional<double> deltaTotalSquaredInterFrameDelay;
			std::optional<double> avgInterFrameDelayInMs;
			std::optional<double> interFrameDelayVariation;
			std::optional<double> deltaPliCount;
			std::optional<double> deltaFirCount;
			std::optional<double> deltaNackCount;
			std::optional<double> deltaRetransmittedBytesReceived;
			std::optional<double> deltaRetransmittedPacketsReceived;
			std::optional<double> retransmissionRatio;
			std::optional<double> decodeTimePerFrameInMs;
			std::optional<double> droppedFrameRatio;
			std::optional<double> renderRatio;
			std::optional<double> keyFrameRate;
			std::optional<double> pliRate;
			std::optional<double> firRate;
			std::optional<double> nackRate;
		};

	public:
		InboundRtpMonitor(
		  PeerConnectionMonitor* peerConnection, const InboundRtpStats& stats, const nlohmann::json& raw);

	public:
		void Accept(const InboundRtpStats& stats, const nlohmann::json& raw);
		const Metrics& GetMetrics() const
		{
			return this->metrics;
		}
		int64_t GetSsrc() const
		{
			return this->stats.ssrc;
		}
		const std::string& GetKind() const
		{
			return this->stats.kind;
		}
		const std::string& GetTrackIdentifier() const
		{
			return this->stats.trackIdentifier;
		}
		// Chromium-only counters, not in the schema.
		std::optional<double> GetInterruptionCount() const
		{
			return this->interruptionCount;
		}
		std::optional<double> GetTotalInterruptionDuration() const
		{
			return this->totalInterruptionDuration;
		}

		InboundTrackMonitor* GetTrack() const;
		CodecMonitor* GetCodec() const;
		RemoteOutboundRtpMonitor* GetRemoteOutboundRtp() const;
		MediaPlayoutMonitor* GetMediaPlayout() const;
		IceTransportMonitor* GetIceTransport() const;
		IceCandidatePairMonitor* GetSelectedCandidatePair() const;

	private:
		std::optional<double> DeriveNormalizedQp() const;

	private:
		Metrics metrics;
		std::deque<double> lastNFramesPerSec;
		std::optional<double> interruptionCount;
		std::optional<double> totalInterruptionDuration;
	};

	/**
	 * One sent RTP stream (`outbound-rtp`); a simulcast track has one per layer.
	 */
	class OutboundRtpMonitor : public StatsMonitor<OutboundRtpStats>
	{
	public:
		struct QualityLimitationShares
		{
			double none{ 0 };
			double cpu{ 0 };
			double bandwidth{ 0 };
			double other{ 0 };
		};

		struct Metrics
		{
			std::optional<double> bitrate;
			std::optional<double> payloadBitrate;
			std::optional<double> packetRate;
			std::optional<double> bitPerPixel;
			std::optional<double> deltaPacketsSent;
			std::optional<double> deltaBytesSent;
			std::optional<double> deltaHeaderBytesSent;
			std::optional<double> deltaRetransmittedPacketsSent;
			std::optional<double> deltaRetransmittedBytesSent;
			std::optional<double> deltaFramesSent;
			std::optional<double> deltaFramesEncoded;
			std::optional<double> deltaKeyFramesEncoded;
			std::optional<double> deltaHugeFramesSent;
			std::optional<double> deltaEncodeTime;
			std::optional<double> deltaPacketSendDelay;
			std::optional<double> deltaQpSum;
			std::optional<double> deltaNackCount;
			std::optional<double> deltaFirCount;
			std::optional<double> deltaPliCount;
			std::optional<double> avgEncodeTimePerFrameInMs;
			std::optional<double> retransmissionRatio;
			std::optional<double> retransmittedPacketRatio;
			std::optional<double> avgQpPerFrame;
			std::optional<double> avgPacketSendDelayInMs;
			std::optional<double> keyFrameRate;
			std::optional<double> nackRate;
			std::optional<double> pliRate;
			std::optional<double> firRate;
			// How the time since the last report split between the limitation reasons.
			std::optional<QualityLimitationShares> qualityLimitationDurationShares;
		};

	public:
		OutboundRtpMonitor(PeerConnectionMonitor* peerConnection, const OutboundRtpStats& stats);

	public:
		void Accept(const OutboundRtpStats& stats);
		const Metrics& GetMetrics() const
		{
			return this->metrics;
		}
		int64_t GetSsrc() const
		{
			return this->stats.ssrc;
		}
		const std::string& GetKind() const
		{
			return this->stats.kind;
		}
		/** The track identifier of the media source this stream encodes, if reported. */
		std::optional<std::string> GetTrackIdentifier() const;

		OutboundTrackMonitor* GetTrack() const;
		CodecMonitor* GetCodec() const;
		MediaSourceMonitor* GetMediaSource() const;
		RemoteInboundRtpMonitor* GetRemoteInboundRtp() const;
		IceTransportMonitor* GetIceTransport() const;
		IceCandidatePairMonitor* GetSelectedCandidatePair() const;

	private:
		Metrics metrics;
	};

	/**
	 * The far end's RTCP receiver report about one of our sent streams (`remote-inbound-rtp`).
	 */
	class RemoteInboundRtpMonitor : public StatsMonitor<RemoteInboundRtpStats>
	{
	public:
		struct Metrics
		{
			std::optional<double> packetRate;
			std::optional<double> deltaPacketsLost;
			std::optional<double> deltaPacketsReceived;
			std::optional<double> deltaFractionLost;
			std::optional<double> deltaTotalRoundTripTime;
			std::optional<double> deltaRoundTripTimeMeasurements;
			std::optional<double> avgRoundTripTimeInSec;
		};

	public:
		RemoteInboundRtpMonitor(PeerConnectionMonitor* peerConnection, const RemoteInboundRtpStats& stats);

	public:
		void Accept(const RemoteInboundRtpStats& stats);
		const Metrics& GetMetrics() const
		{
			return this->metrics;
		}
		int64_t GetSsrc() const
		{
			return this->stats.ssrc;
		}

		OutboundRtpMonitor* GetOutboundRtp() const;
		CodecMonitor* GetCodec() const;
		IceTransportMonitor* GetIceTransport() const;
		IceCandidatePairMonitor* GetSelectedCandidatePair() const;

	private:
		Metrics metrics;
	};

	/**
	 * The far end's RTCP sender report about one of our received streams (`remote-outbound-rtp`).
	 */
	class RemoteOutboundRtpMonitor : public StatsMonitor<RemoteOutboundRtpStats>
	{
	public:
		struct Metrics
		{
			std::optional<double> deltaPacketsSent;
			std::optional<double> deltaBytesSent;
		};

	public:
		RemoteOutboundRtpMonitor(PeerConnectionMonitor* peerConnection, const RemoteOutboundRtpStats& stats);

	public:
		void Accept(const RemoteOutboundRtpStats& stats);
		const Metrics& GetMetrics() const
		{
			return this->metrics;
		}
		int64_t GetSsrc() const
		{
			return this->stats.ssrc;
		}

		InboundRtpMonitor* GetInboundRtp() const;
		CodecMonitor* GetCodec() const;
		IceTransportMonitor* GetIceTransport() const;
		IceCandidatePairMonitor* GetSelectedCandidatePair() const;

	private:
		Metrics metrics;
	};
} // namespace clientmonitor

#endif
