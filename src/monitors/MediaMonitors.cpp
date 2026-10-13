#define CM_CLASS "MediaMonitors"

#include "clientmonitor/monitors/MediaMonitors.hpp"
#include "clientmonitor/utils/Utils.hpp"
#include "clientmonitor/Logger.hpp"
#include "clientmonitor/monitors/PeerConnectionMonitor.hpp"
#include "clientmonitor/monitors/RtpMonitors.hpp"

#include <cmath>

using observertc::clientmonitor::utils::PositiveDelta;

namespace observertc::clientmonitor
{
	/* CodecMonitor */

	CodecMonitor::CodecMonitor(PeerConnectionMonitor* peerConnection, const CodecStats& stats)
	  : StatsMonitor<CodecStats>(peerConnection, stats)
	{
	}

	void CodecMonitor::Accept(const CodecStats& next)
	{
		if (!this->Visit(next).has_value())
		{
			return;
		}

		this->stats = next;
	}

	IceTransportMonitor* CodecMonitor::GetIceTransport() const
	{
		return this->peerConnection->GetIceTransportMonitor(this->stats.transportId.value_or(""));
	}

	/* MediaSourceMonitor */

	MediaSourceMonitor::MediaSourceMonitor(
	  PeerConnectionMonitor* peerConnection, const MediaSourceStats& stats)
	  : StatsMonitor<MediaSourceStats>(peerConnection, stats)
	{
	}

	void MediaSourceMonitor::Accept(const MediaSourceStats& next)
	{
		const auto elapsed = this->Visit(next);

		if (!elapsed.has_value())
		{
			return;
		}

		const auto& prev          = this->stats;
		auto& m                   = this->metrics;
		const double elapsedInSec = *elapsed / 1000;

		m.deltaFrames           = PositiveDelta(next.frames, prev.frames);
		m.deltaTotalAudioEnergy = PositiveDelta(next.totalAudioEnergy, prev.totalAudioEnergy);
		m.deltaSamplesDuration  = PositiveDelta(next.totalSamplesDuration, prev.totalSamplesDuration);
		m.producedFps = m.deltaFrames.has_value() ? std::optional<double>(*m.deltaFrames / elapsedInSec)
		                                          : std::nullopt;
		m.rmsAudioLevel =
		  m.deltaTotalAudioEnergy.has_value() && m.deltaSamplesDuration.has_value() &&
		      *m.deltaSamplesDuration > 0
		    ? std::optional<double>(std::sqrt(*m.deltaTotalAudioEnergy / *m.deltaSamplesDuration))
		    : std::nullopt;

		this->stats = next;
	}

	OutboundTrackMonitor* MediaSourceMonitor::GetTrack() const
	{
		return this->peerConnection->GetOutboundTrackMonitor(this->stats.trackIdentifier.value_or(""));
	}

	std::vector<OutboundRtpMonitor*> MediaSourceMonitor::GetOutboundRtps() const
	{
		std::vector<OutboundRtpMonitor*> result;

		for (auto* rtp : this->peerConnection->GetOutboundRtps())
		{
			if (rtp->GetStats().mediaSourceId == this->stats.id)
			{
				result.push_back(rtp);
			}
		}

		return result;
	}

	/* MediaPlayoutMonitor */

	MediaPlayoutMonitor::MediaPlayoutMonitor(
	  PeerConnectionMonitor* peerConnection, const MediaPlayoutStats& stats)
	  : StatsMonitor<MediaPlayoutStats>(peerConnection, stats)
	{
	}

	void MediaPlayoutMonitor::Accept(const MediaPlayoutStats& next)
	{
		if (!this->Visit(next).has_value())
		{
			return;
		}

		const auto& prev = this->stats;
		auto& m          = this->metrics;

		m.deltaSynthesizedSamplesDuration =
		  PositiveDelta(next.synthesizedSamplesDuration, prev.synthesizedSamplesDuration).value_or(0);
		m.deltaSamplesDuration =
		  PositiveDelta(next.totalSamplesDuration, prev.totalSamplesDuration).value_or(0);
		m.deltaSynthesizedSamplesEvents =
		  PositiveDelta(next.synthesizedSamplesEvents, prev.synthesizedSamplesEvents);
		m.deltaTotalPlayoutDelay = PositiveDelta(next.totalPlayoutDelay, prev.totalPlayoutDelay);
		m.deltaSamplesCount      = PositiveDelta(next.totalSamplesCount, prev.totalSamplesCount);
		m.playoutDelayPerSampleInMs =
		  m.deltaTotalPlayoutDelay.has_value() && m.deltaSamplesCount.has_value() &&
		      *m.deltaSamplesCount > 0
		    ? std::optional<double>((*m.deltaTotalPlayoutDelay * 1000) / *m.deltaSamplesCount)
		    : std::nullopt;
		m.synthesizedSamplesRatio =
		  m.deltaSamplesDuration > 0
		    ? std::min(1.0, m.deltaSynthesizedSamplesDuration / m.deltaSamplesDuration)
		    : 0;

		this->stats = next;

		this->detectors.Update();
	}

	/* DataChannelMonitor */

	DataChannelMonitor::DataChannelMonitor(
	  PeerConnectionMonitor* peerConnection, const DataChannelStats& stats)
	  : StatsMonitor<DataChannelStats>(peerConnection, stats)
	{
	}

	void DataChannelMonitor::Accept(const DataChannelStats& next)
	{
		const auto elapsed = this->Visit(next);

		if (!elapsed.has_value())
		{
			return;
		}

		const double elapsedInSec = *elapsed / 1000;
		auto& m                   = this->metrics;

		m.deltaBytesReceived = PositiveDelta(next.bytesReceived, this->stats.bytesReceived);
		m.deltaBytesSent     = PositiveDelta(next.bytesSent, this->stats.bytesSent);

		if (m.deltaBytesSent.has_value())
		{
			m.sendingBitrate = std::max(0.0, *m.deltaBytesSent * 8 / elapsedInSec);
		}
		if (m.deltaBytesReceived.has_value())
		{
			m.receivingBitrate = std::max(0.0, *m.deltaBytesReceived * 8 / elapsedInSec);
		}

		this->stats = next;
	}

	/* PeerConnectionTransportMonitor */

	PeerConnectionTransportMonitor::PeerConnectionTransportMonitor(
	  PeerConnectionMonitor* peerConnection, const PeerConnectionTransportStats& stats)
	  : StatsMonitor<PeerConnectionTransportStats>(peerConnection, stats)
	{
	}

	void PeerConnectionTransportMonitor::Accept(const PeerConnectionTransportStats& next)
	{
		if (!this->Visit(next).has_value())
		{
			return;
		}

		this->stats = next;
	}

	/* CertificateMonitor */

	CertificateMonitor::CertificateMonitor(
	  PeerConnectionMonitor* peerConnection, const CertificateStats& stats)
	  : StatsMonitor<CertificateStats>(peerConnection, stats)
	{
	}

	void CertificateMonitor::Accept(const CertificateStats& next)
	{
		if (!this->Visit(next).has_value())
		{
			return;
		}

		this->stats = next;
	}
} // namespace observertc::clientmonitor
