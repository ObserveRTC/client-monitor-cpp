#define CM_CLASS "RtpMonitors"

#include "clientmonitor/monitors/RtpMonitors.hpp"
#include "clientmonitor/utils/JsonReader.hpp"
#include "clientmonitor/utils/MediaMath.hpp"
#include "clientmonitor/utils/Utils.hpp"
#include "clientmonitor/Logger.hpp"
#include "clientmonitor/monitors/IceMonitors.hpp"
#include "clientmonitor/monitors/MediaMonitors.hpp"
#include "clientmonitor/monitors/PeerConnectionMonitor.hpp"

#include <cmath>
#include <numeric>

using observertc::clientmonitor::utils::PositiveDelta;
using observertc::clientmonitor::utils::ToDouble;

namespace observertc::clientmonitor
{
	/* InboundRtpMonitor */

	InboundRtpMonitor::InboundRtpMonitor(
	  PeerConnectionMonitor* peerConnection, const InboundRtpStats& stats, const nlohmann::json& raw)
	  : StatsMonitor<InboundRtpStats>(peerConnection, stats)
	{
		this->interruptionCount         = detail::ReadDouble(raw, "interruptionCount");
		this->totalInterruptionDuration = detail::ReadDouble(raw, "totalInterruptionDuration");
	}

	void InboundRtpMonitor::Accept(const InboundRtpStats& next, const nlohmann::json& raw)
	{
		const auto elapsed = this->Visit(next);

		// Not in the schema: Chromium reports them on audio `inbound-rtp`.
		const auto nextInterruptionCount         = detail::ReadDouble(raw, "interruptionCount");
		const auto nextTotalInterruptionDuration = detail::ReadDouble(raw, "totalInterruptionDuration");

		if (!elapsed.has_value())
		{
			// No interval passed, so nothing derived can be recomputed, but still take the report
			// so this monitor always holds the latest one seen.
			this->stats                     = next;
			this->interruptionCount         = nextInterruptionCount;
			this->totalInterruptionDuration = nextTotalInterruptionDuration;

			return;
		}

		const auto& prev          = this->stats;
		auto& m                   = this->metrics;
		const double elapsedInMs  = *elapsed;
		const double elapsedInSec = elapsedInMs / 1000;

		// Deltas first: they compare the incoming report against the one still held.
		m.deltaTotalSamplesReceived = PositiveDelta(next.totalSamplesReceived, prev.totalSamplesReceived);

		if (m.deltaTotalSamplesReceived.has_value())
		{
			m.receivingAudioSamples = m.deltaTotalSamplesReceived;
		}
		if (prev.bytesReceived.has_value() && next.bytesReceived.has_value())
		{
			m.deltaBytesReceived = PositiveDelta(next.bytesReceived, prev.bytesReceived);
			// A counter reset leaves the delta undefined: no bitrate rather than a stale one.
			m.bitrate = m.deltaBytesReceived.has_value()
			              ? std::optional<double>(std::max(0.0, *m.deltaBytesReceived * 8 / elapsedInSec))
			              : std::nullopt;
		}
		if (prev.packetsLost.has_value() && next.packetsLost.has_value())
		{
			m.deltaPacketsLost = PositiveDelta(next.packetsLost, prev.packetsLost);
		}
		if (prev.packetsReceived.has_value() && next.packetsReceived.has_value())
		{
			m.deltaPacketsReceived = PositiveDelta(next.packetsReceived, prev.packetsReceived);
			m.packetRate           = m.deltaPacketsReceived.has_value()
			                           ? std::optional<double>(*m.deltaPacketsReceived / elapsedInSec)
			                           : std::nullopt;
		}

		// Audio: concealment and jitter-buffer pressure.
		m.deltaConcealedSamples = PositiveDelta(next.concealedSamples, prev.concealedSamples);
		m.deltaSilentConcealedSamples =
		  PositiveDelta(next.silentConcealedSamples, prev.silentConcealedSamples);
		m.deltaConcealmentEvents = PositiveDelta(next.concealmentEvents, prev.concealmentEvents);
		m.deltaInsertedSamplesForDeceleration =
		  PositiveDelta(next.insertedSamplesForDeceleration, prev.insertedSamplesForDeceleration);
		m.deltaRemovedSamplesForAcceleration =
		  PositiveDelta(next.removedSamplesForAcceleration, prev.removedSamplesForAcceleration);
		m.deltaPacketsDiscarded = PositiveDelta(next.packetsDiscarded, prev.packetsDiscarded);
		m.deltaJitterBufferEmittedCount =
		  PositiveDelta(next.jitterBufferEmittedCount, prev.jitterBufferEmittedCount);

		if (m.deltaConcealedSamples.has_value() && 0 < m.deltaTotalSamplesReceived.value_or(0))
		{
			// Silent concealment is subtracted: `concealedSamples` also rises during ordinary silence.
			const double nonSilent =
			  std::max(0.0, *m.deltaConcealedSamples - m.deltaSilentConcealedSamples.value_or(0));

			m.nonSilentConcealedRatio = nonSilent / *m.deltaTotalSamplesReceived;
		}
		else
		{
			m.nonSilentConcealedRatio.reset();
		}

		m.deltaInterruptionCount = PositiveDelta(nextInterruptionCount, this->interruptionCount);

		const auto deltaInterruptionDuration =
		  PositiveDelta(nextTotalInterruptionDuration, this->totalInterruptionDuration);

		m.deltaTotalInterruptionDurationInMs =
		  deltaInterruptionDuration.has_value()
		    ? std::optional<double>(*deltaInterruptionDuration * 1000)
		    : std::nullopt;
		m.concealmentEventRate = m.deltaConcealmentEvents.has_value()
		                           ? std::optional<double>(*m.deltaConcealmentEvents / elapsedInSec)
		                           : std::nullopt;

		if (0 < m.deltaTotalSamplesReceived.value_or(0))
		{
			const double stretched = m.deltaInsertedSamplesForDeceleration.value_or(0) +
			                         m.deltaRemovedSamplesForAcceleration.value_or(0);

			m.timeStretchRate = stretched / *m.deltaTotalSamplesReceived;
		}
		else
		{
			m.timeStretchRate.reset();
		}

		if (m.deltaPacketsDiscarded.has_value())
		{
			const double consumed = *m.deltaPacketsDiscarded + m.deltaPacketsReceived.value_or(0);

			m.discardRate = 0 < consumed ? *m.deltaPacketsDiscarded / consumed : 0;
		}
		if (
		  prev.totalCorruptionProbability.has_value() && next.totalCorruptionProbability.has_value() &&
		  prev.corruptionMeasurements.has_value() && next.corruptionMeasurements.has_value())
		{
			const double deltaCorruption =
			  *next.totalCorruptionProbability - *prev.totalCorruptionProbability;
			const double deltaMeasurements = std::max<double>(
			  1, static_cast<double>(*next.corruptionMeasurements - *prev.corruptionMeasurements));

			m.deltaCorruptionProbability = std::max(0.0, deltaCorruption / deltaMeasurements);
		}

		m.deltaJitterBufferDelay = PositiveDelta(next.jitterBufferDelay, prev.jitterBufferDelay);
		m.deltaFramesDecoded     = PositiveDelta(next.framesDecoded, prev.framesDecoded);
		m.deltaFramesReceived    = PositiveDelta(next.framesReceived, prev.framesReceived);
		m.deltaFramesRendered    = PositiveDelta(next.framesRendered, prev.framesRendered);
		m.deltaFramesDropped     = PositiveDelta(next.framesDropped, prev.framesDropped);
		m.deltaKeyFramesDecoded  = PositiveDelta(next.keyFramesDecoded, prev.keyFramesDecoded);
		m.deltaQpSum             = PositiveDelta(next.qpSum, prev.qpSum);

		if (m.deltaQpSum.has_value() && 0 < m.deltaFramesDecoded.value_or(0))
		{
			m.avgQpPerFrame = *m.deltaQpSum / *m.deltaFramesDecoded;
		}
		else
		{
			m.avgQpPerFrame.reset();
		}

		m.normalizedQp         = this->DeriveNormalizedQp();
		m.deltaTotalDecodeTime = PositiveDelta(next.totalDecodeTime, prev.totalDecodeTime);
		m.deltaTotalFreezesDuration = PositiveDelta(next.totalFreezesDuration, prev.totalFreezesDuration);
		m.deltaFreezeCount         = PositiveDelta(next.freezeCount, prev.freezeCount);
		m.frozenTimeRatio          = m.deltaTotalFreezesDuration.has_value()
		                               ? std::optional<double>(*m.deltaTotalFreezesDuration / elapsedInSec)
		                               : std::nullopt;
		m.deltaPauseCount          = PositiveDelta(next.pauseCount, prev.pauseCount);
		m.deltaTotalPausesDuration = PositiveDelta(next.totalPausesDuration, prev.totalPausesDuration);
		m.pausedTimeRatio          = m.deltaTotalPausesDuration.has_value()
		                               ? std::optional<double>(*m.deltaTotalPausesDuration / elapsedInSec)
		                               : std::nullopt;
		m.deltaTotalInterFrameDelay = PositiveDelta(next.totalInterFrameDelay, prev.totalInterFrameDelay);
		m.deltaTotalSquaredInterFrameDelay =
		  PositiveDelta(next.totalSquaredInterFrameDelay, prev.totalSquaredInterFrameDelay);

		// Mean and spread of the inter-frame gap, from the per-frame sums.
		if (
		  m.deltaTotalInterFrameDelay.has_value() && m.deltaTotalSquaredInterFrameDelay.has_value() &&
		  1 < m.deltaFramesDecoded.value_or(0))
		{
			const double frames = *m.deltaFramesDecoded;
			const double mean   = *m.deltaTotalInterFrameDelay / frames;
			// Clamped: the two sums are independently accumulated floats.
			const double variance =
			  std::max(0.0, *m.deltaTotalSquaredInterFrameDelay / frames - mean * mean);

			m.avgInterFrameDelayInMs = mean * 1000;
			m.interFrameDelayVariation =
			  0 < mean ? std::optional<double>(std::sqrt(variance) / mean) : std::nullopt;
		}
		else
		{
			m.avgInterFrameDelayInMs.reset();
			m.interFrameDelayVariation.reset();
		}

		m.deltaPliCount  = PositiveDelta(next.pliCount, prev.pliCount);
		m.deltaFirCount  = PositiveDelta(next.firCount, prev.firCount);
		m.deltaNackCount = PositiveDelta(next.nackCount, prev.nackCount);
		m.deltaRetransmittedBytesReceived =
		  PositiveDelta(next.retransmittedBytesReceived, prev.retransmittedBytesReceived);
		m.deltaRetransmittedPacketsReceived =
		  PositiveDelta(next.retransmittedPacketsReceived, prev.retransmittedPacketsReceived);

		m.retransmissionRatio =
		  m.deltaRetransmittedBytesReceived.has_value() && 0 < m.deltaBytesReceived.value_or(0)
		    ? std::optional<double>(
		        std::min(1.0, *m.deltaRetransmittedBytesReceived / *m.deltaBytesReceived))
		    : std::nullopt;

		m.avgJitterBufferDelayInMs =
		  0 < m.deltaJitterBufferEmittedCount.value_or(0) && m.deltaJitterBufferDelay.has_value()
		    ? std::optional<double>((*m.deltaJitterBufferDelay / *m.deltaJitterBufferEmittedCount) * 1000)
		    : std::nullopt;
		m.deltaJitterBufferTargetDelay =
		  PositiveDelta(next.jitterBufferTargetDelay, prev.jitterBufferTargetDelay);
		m.jitterBufferTargetDelayInMs =
		  0 < m.deltaJitterBufferEmittedCount.value_or(0) && m.deltaJitterBufferTargetDelay.has_value()
		    ? std::optional<double>(
		        (*m.deltaJitterBufferTargetDelay / *m.deltaJitterBufferEmittedCount) * 1000)
		    : std::nullopt;

		// Video: decode cost and recovery pressure.
		m.decodeTimePerFrameInMs =
		  0 < m.deltaFramesDecoded.value_or(0) && m.deltaTotalDecodeTime.has_value()
		    ? std::optional<double>((*m.deltaTotalDecodeTime / *m.deltaFramesDecoded) * 1000)
		    : std::nullopt;
		m.droppedFrameRatio = 0 < m.deltaFramesReceived.value_or(0) && m.deltaFramesDropped.has_value()
		                        ? std::optional<double>(*m.deltaFramesDropped / *m.deltaFramesReceived)
		                        : std::nullopt;
		m.renderRatio       = 0 < m.deltaFramesDecoded.value_or(0) && m.deltaFramesRendered.has_value()
		                        ? std::optional<double>(*m.deltaFramesRendered / *m.deltaFramesDecoded)
		                        : std::nullopt;
		m.keyFrameRate      = m.deltaKeyFramesDecoded.has_value()
		                        ? std::optional<double>(*m.deltaKeyFramesDecoded / elapsedInSec)
		                        : std::nullopt;
		m.pliRate = m.deltaPliCount.has_value() ? std::optional<double>(*m.deltaPliCount / elapsedInSec)
		                                        : std::nullopt;
		m.firRate = m.deltaFirCount.has_value() ? std::optional<double>(*m.deltaFirCount / elapsedInSec)
		                                        : std::nullopt;
		m.nackRate = m.deltaNackCount.has_value()
		               ? std::optional<double>(*m.deltaNackCount / elapsedInSec)
		               : std::nullopt;

		this->stats                     = next;
		this->interruptionCount         = nextInterruptionCount;
		this->totalInterruptionDuration = nextTotalInterruptionDuration;

		const auto& s = this->stats;

		if (s.framesPerSecond.has_value() && *s.framesPerSecond != 0)
		{
			this->lastNFramesPerSec.push_back(*s.framesPerSecond);

			if (this->lastNFramesPerSec.size() > 10)
			{
				this->lastNFramesPerSec.pop_front();
			}

			const double n = static_cast<double>(this->lastNFramesPerSec.size());
			const double avgFramesPerSec =
			  std::accumulate(this->lastNFramesPerSec.begin(), this->lastNFramesPerSec.end(), 0.0) / n;
			double avgDiff{ 0 };

			for (double fps : this->lastNFramesPerSec)
			{
				avgDiff += std::abs(fps - avgFramesPerSec);
			}

			avgDiff /= n;

			m.avgFramesPerSec = avgFramesPerSec;
			m.fpsVolatility   = avgDiff / avgFramesPerSec;

			if (m.bitrate.value_or(0) != 0 && s.frameWidth.value_or(0) != 0 && s.frameHeight.value_or(0) != 0)
			{
				m.bitPerPixel = *m.bitrate / (static_cast<double>(*s.frameWidth) *
				                              static_cast<double>(*s.frameHeight) * *s.framesPerSecond);
			}
		}

		if (s.packetsReceived.has_value() && s.packetsLost.has_value())
		{
			const auto received = static_cast<double>(*s.packetsReceived);
			const auto lost     = static_cast<double>(*s.packetsLost);

			m.totalFractionLost = 0 < received && 0 < lost ? lost / (lost + received) : 0.0;
		}
		if (m.deltaPacketsReceived.has_value() && m.deltaPacketsLost.has_value())
		{
			m.deltaFractionLost = 0 < *m.deltaPacketsReceived && 0 < *m.deltaPacketsLost
			                        ? *m.deltaPacketsLost / (*m.deltaPacketsLost + *m.deltaPacketsReceived)
			                        : 0.0;
		}
		if (s.framesPerSecond.has_value())
		{
			m.ewmaFps = m.ewmaFps.value_or(0) != 0 ? 0.9 * *m.ewmaFps + 0.1 * *s.framesPerSecond
			                                       : *s.framesPerSecond;
		}
	}

	std::optional<double> InboundRtpMonitor::DeriveNormalizedQp() const
	{
		if (!this->metrics.avgQpPerFrame.has_value())
		{
			return std::nullopt;
		}

		const auto* codec = this->GetCodec();
		const auto qpScale =
		  utils::QpScaleOf(codec ? std::optional<std::string>(codec->GetStats().mimeType) : std::nullopt);

		if (!qpScale.has_value() || *qpScale <= 0)
		{
			return std::nullopt;
		}

		return utils::Clamp(*this->metrics.avgQpPerFrame / *qpScale, 0, 1);
	}

	InboundTrackMonitor* InboundRtpMonitor::GetTrack() const
	{
		return this->peerConnection->GetInboundTrackMonitor(this->stats.trackIdentifier);
	}

	CodecMonitor* InboundRtpMonitor::GetCodec() const
	{
		return this->peerConnection->GetCodecMonitor(this->stats.codecId.value_or(""));
	}

	RemoteOutboundRtpMonitor* InboundRtpMonitor::GetRemoteOutboundRtp() const
	{
		return this->peerConnection->GetRemoteOutboundRtpMonitor(this->stats.ssrc);
	}

	MediaPlayoutMonitor* InboundRtpMonitor::GetMediaPlayout() const
	{
		return this->peerConnection->GetMediaPlayoutMonitor(this->stats.playoutId.value_or(""));
	}

	IceTransportMonitor* InboundRtpMonitor::GetIceTransport() const
	{
		return this->peerConnection->GetIceTransportMonitor(this->stats.transportId.value_or(""));
	}

	IceCandidatePairMonitor* InboundRtpMonitor::GetSelectedCandidatePair() const
	{
		auto* transport = this->GetIceTransport();

		return transport ? transport->GetSelectedCandidatePair() : nullptr;
	}

	/* OutboundRtpMonitor */

	OutboundRtpMonitor::OutboundRtpMonitor(
	  PeerConnectionMonitor* peerConnection, const OutboundRtpStats& stats)
	  : StatsMonitor<OutboundRtpStats>(peerConnection, stats)
	{
	}

	void OutboundRtpMonitor::Accept(const OutboundRtpStats& next)
	{
		const auto elapsed = this->Visit(next);

		if (!elapsed.has_value())
		{
			return;
		}

		const auto& prev          = this->stats;
		auto& m                   = this->metrics;
		const double elapsedInSec = *elapsed / 1000;

		m.deltaPacketsSent = PositiveDelta(next.packetsSent, prev.packetsSent);

		if (m.deltaPacketsSent.has_value())
		{
			m.packetRate = *m.deltaPacketsSent / elapsedInSec;
		}

		m.deltaBytesSent       = PositiveDelta(next.bytesSent, prev.bytesSent);
		m.deltaHeaderBytesSent = PositiveDelta(next.headerBytesSent, prev.headerBytesSent);
		m.deltaRetransmittedBytesSent =
		  PositiveDelta(next.retransmittedBytesSent, prev.retransmittedBytesSent);
		m.deltaRetransmittedPacketsSent =
		  PositiveDelta(next.retransmittedPacketsSent, prev.retransmittedPacketsSent);

		if (m.deltaBytesSent.has_value())
		{
			m.bitrate = std::max(0.0, *m.deltaBytesSent * 8 / elapsedInSec);

			if (m.deltaHeaderBytesSent.has_value())
			{
				const double payloadBytesSent =
				  *m.deltaBytesSent - *m.deltaHeaderBytesSent - m.deltaRetransmittedBytesSent.value_or(0);

				m.payloadBitrate = std::max(0.0, payloadBytesSent * 8 / elapsedInSec);
			}

			m.retransmissionRatio = *m.deltaBytesSent > 0 && m.deltaRetransmittedBytesSent.has_value()
			                          ? std::min(1.0, *m.deltaRetransmittedBytesSent / *m.deltaBytesSent)
			                          : 0;
		}
		if (m.deltaPacketsSent.has_value() && m.deltaRetransmittedPacketsSent.has_value())
		{
			m.retransmittedPacketRatio =
			  *m.deltaPacketsSent > 0
			    ? std::min(1.0, *m.deltaRetransmittedPacketsSent / *m.deltaPacketsSent)
			    : 0;
		}

		m.deltaFramesSent       = PositiveDelta(next.framesSent, prev.framesSent);
		m.deltaFramesEncoded    = PositiveDelta(next.framesEncoded, prev.framesEncoded);
		m.deltaKeyFramesEncoded = PositiveDelta(next.keyFramesEncoded, prev.keyFramesEncoded);
		m.deltaHugeFramesSent   = PositiveDelta(next.hugeFramesSent, prev.hugeFramesSent);
		m.deltaEncodeTime       = PositiveDelta(next.totalEncodeTime, prev.totalEncodeTime);
		m.deltaPacketSendDelay  = PositiveDelta(next.totalPacketSendDelay, prev.totalPacketSendDelay);
		m.deltaQpSum            = PositiveDelta(next.qpSum, prev.qpSum);
		m.deltaNackCount        = PositiveDelta(next.nackCount, prev.nackCount);
		m.deltaFirCount         = PositiveDelta(next.firCount, prev.firCount);
		m.deltaPliCount         = PositiveDelta(next.pliCount, prev.pliCount);

		if (m.deltaFramesEncoded.has_value() && *m.deltaFramesEncoded > 0)
		{
			if (m.deltaEncodeTime.has_value())
			{
				m.avgEncodeTimePerFrameInMs = (*m.deltaEncodeTime * 1000) / *m.deltaFramesEncoded;
			}
			if (m.deltaQpSum.has_value())
			{
				m.avgQpPerFrame = *m.deltaQpSum / *m.deltaFramesEncoded;
			}
		}
		if (m.deltaPacketSendDelay.has_value() && m.deltaPacketsSent.has_value() && *m.deltaPacketsSent > 0)
		{
			m.avgPacketSendDelayInMs = (*m.deltaPacketSendDelay * 1000) / *m.deltaPacketsSent;
		}
		if (m.deltaKeyFramesEncoded.has_value())
		{
			m.keyFrameRate = *m.deltaKeyFramesEncoded / elapsedInSec;
		}
		if (m.deltaNackCount.has_value())
		{
			m.nackRate = *m.deltaNackCount / elapsedInSec;
		}
		if (m.deltaPliCount.has_value())
		{
			m.pliRate = *m.deltaPliCount / elapsedInSec;
		}
		if (m.deltaFirCount.has_value())
		{
			m.firRate = *m.deltaFirCount / elapsedInSec;
		}

		m.qualityLimitationDurationShares.reset();

		if (prev.qualityLimitationDurations.has_value() && next.qualityLimitationDurations.has_value())
		{
			const auto& p   = *prev.qualityLimitationDurations;
			const auto& c   = *next.qualityLimitationDurations;
			const auto diff = [](double a, double b)
			{
				return std::max(0.0, a - b);
			};
			const double none  = diff(c.none, p.none);
			const double cpu   = diff(c.cpu, p.cpu);
			const double bw    = diff(c.bandwidth, p.bandwidth);
			const double other = diff(c.other, p.other);
			const double total = none + cpu + bw + other;

			if (total > 0)
			{
				m.qualityLimitationDurationShares =
				  QualityLimitationShares{ none / total, cpu / total, bw / total, other / total };
			}
		}

		this->stats = next;

		const auto& s = this->stats;

		if (
		  s.frameWidth.value_or(0) != 0 && s.frameHeight.value_or(0) != 0 &&
		  s.framesPerSecond.value_or(0) != 0 && m.bitrate.value_or(0) != 0)
		{
			m.bitPerPixel = *m.bitrate / (static_cast<double>(*s.frameHeight) *
			                              static_cast<double>(*s.frameWidth) * *s.framesPerSecond);
		}
	}

	std::optional<std::string> OutboundRtpMonitor::GetTrackIdentifier() const
	{
		auto* mediaSource = this->GetMediaSource();

		return mediaSource ? mediaSource->GetStats().trackIdentifier : std::nullopt;
	}

	OutboundTrackMonitor* OutboundRtpMonitor::GetTrack() const
	{
		auto* mediaSource = this->GetMediaSource();

		if (mediaSource && mediaSource->GetTrack())
		{
			return mediaSource->GetTrack();
		}

		const auto trackIdentifier = this->GetTrackIdentifier();

		return this->peerConnection->GetOutboundTrackMonitor(trackIdentifier.value_or(""));
	}

	CodecMonitor* OutboundRtpMonitor::GetCodec() const
	{
		return this->peerConnection->GetCodecMonitor(this->stats.codecId.value_or(""));
	}

	MediaSourceMonitor* OutboundRtpMonitor::GetMediaSource() const
	{
		return this->peerConnection->GetMediaSourceMonitor(this->stats.mediaSourceId.value_or(""));
	}

	RemoteInboundRtpMonitor* OutboundRtpMonitor::GetRemoteInboundRtp() const
	{
		return this->peerConnection->GetRemoteInboundRtpMonitor(this->stats.ssrc);
	}

	IceTransportMonitor* OutboundRtpMonitor::GetIceTransport() const
	{
		return this->peerConnection->GetIceTransportMonitor(this->stats.transportId.value_or(""));
	}

	IceCandidatePairMonitor* OutboundRtpMonitor::GetSelectedCandidatePair() const
	{
		auto* transport = this->GetIceTransport();

		return transport ? transport->GetSelectedCandidatePair() : nullptr;
	}

	/* RemoteInboundRtpMonitor */

	RemoteInboundRtpMonitor::RemoteInboundRtpMonitor(
	  PeerConnectionMonitor* peerConnection, const RemoteInboundRtpStats& stats)
	  : StatsMonitor<RemoteInboundRtpStats>(peerConnection, stats)
	{
	}

	void RemoteInboundRtpMonitor::Accept(const RemoteInboundRtpStats& next)
	{
		const auto elapsed = this->Visit(next);
		auto& m            = this->metrics;

		if (!elapsed.has_value())
		{
			// The same receiver report served again: nothing new about the far end.
			this->deltaTime = 0;
			m               = Metrics{};

			return;
		}

		const auto& prev              = this->stats;
		const double elapsedInSeconds = *elapsed / 1000;

		m.deltaPacketsReceived = PositiveDelta(next.packetsReceived, prev.packetsReceived);

		if (m.deltaPacketsReceived.has_value())
		{
			m.packetRate = *m.deltaPacketsReceived / elapsedInSeconds;
		}

		m.deltaPacketsLost        = PositiveDelta(next.packetsLost, prev.packetsLost);
		m.deltaTotalRoundTripTime = PositiveDelta(next.totalRoundTripTime, prev.totalRoundTripTime);
		m.deltaRoundTripTimeMeasurements =
		  PositiveDelta(next.roundTripTimeMeasurements, prev.roundTripTimeMeasurements);
		m.avgRoundTripTimeInSec =
		  m.deltaTotalRoundTripTime.has_value() && m.deltaRoundTripTimeMeasurements.has_value() &&
		      *m.deltaRoundTripTimeMeasurements > 0
		    ? std::optional<double>(*m.deltaTotalRoundTripTime / *m.deltaRoundTripTimeMeasurements)
		    : std::nullopt;

		if (m.deltaPacketsReceived.has_value() && m.deltaPacketsLost.has_value())
		{
			const double totalDelta = *m.deltaPacketsReceived + *m.deltaPacketsLost;

			m.deltaFractionLost = totalDelta > 0 ? *m.deltaPacketsLost / totalDelta : 0.0;
		}

		this->stats = next;
	}

	OutboundRtpMonitor* RemoteInboundRtpMonitor::GetOutboundRtp() const
	{
		return this->peerConnection->GetOutboundRtpMonitor(this->stats.ssrc);
	}

	CodecMonitor* RemoteInboundRtpMonitor::GetCodec() const
	{
		return this->peerConnection->GetCodecMonitor(this->stats.codecId.value_or(""));
	}

	IceTransportMonitor* RemoteInboundRtpMonitor::GetIceTransport() const
	{
		return this->peerConnection->GetIceTransportMonitor(this->stats.transportId.value_or(""));
	}

	IceCandidatePairMonitor* RemoteInboundRtpMonitor::GetSelectedCandidatePair() const
	{
		auto* transport = this->GetIceTransport();

		return transport ? transport->GetSelectedCandidatePair() : nullptr;
	}

	/* RemoteOutboundRtpMonitor */

	RemoteOutboundRtpMonitor::RemoteOutboundRtpMonitor(
	  PeerConnectionMonitor* peerConnection, const RemoteOutboundRtpStats& stats)
	  : StatsMonitor<RemoteOutboundRtpStats>(peerConnection, stats)
	{
	}

	void RemoteOutboundRtpMonitor::Accept(const RemoteOutboundRtpStats& next)
	{
		const auto elapsed = this->Visit(next);

		if (!elapsed.has_value())
		{
			this->deltaTime = 0;
			this->metrics   = Metrics{};

			return;
		}

		this->metrics.deltaPacketsSent = PositiveDelta(next.packetsSent, this->stats.packetsSent);
		this->metrics.deltaBytesSent   = PositiveDelta(next.bytesSent, this->stats.bytesSent);

		this->stats = next;
	}

	InboundRtpMonitor* RemoteOutboundRtpMonitor::GetInboundRtp() const
	{
		return this->peerConnection->GetInboundRtpMonitor(this->stats.ssrc);
	}

	CodecMonitor* RemoteOutboundRtpMonitor::GetCodec() const
	{
		return this->peerConnection->GetCodecMonitor(this->stats.codecId.value_or(""));
	}

	IceTransportMonitor* RemoteOutboundRtpMonitor::GetIceTransport() const
	{
		return this->peerConnection->GetIceTransportMonitor(this->stats.transportId.value_or(""));
	}

	IceCandidatePairMonitor* RemoteOutboundRtpMonitor::GetSelectedCandidatePair() const
	{
		auto* transport = this->GetIceTransport();

		return transport ? transport->GetSelectedCandidatePair() : nullptr;
	}
} // namespace observertc::clientmonitor
