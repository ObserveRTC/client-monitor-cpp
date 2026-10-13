#ifndef CM_MONITORS_MEDIA_MONITORS_HPP
#define CM_MONITORS_MEDIA_MONITORS_HPP

#include "clientmonitor/detectors/Detector.hpp"
#include "clientmonitor/monitors/StatsMonitor.hpp"
#include "clientmonitor/schema/SchemaTypes.hpp"

#include <optional>
#include <vector>

namespace observertc::clientmonitor
{
	// Fast forward declarations.
	class IceTransportMonitor;
	class OutboundRtpMonitor;
	class OutboundTrackMonitor;

	/** One negotiated codec (`codec`). */
	class CodecMonitor : public StatsMonitor<CodecStats>
	{
	public:
		CodecMonitor(PeerConnectionMonitor* peerConnection, const CodecStats& stats);

	public:
		void Accept(const CodecStats& stats);
		IceTransportMonitor* GetIceTransport() const;
	};

	/** One local capture source (`media-source`). */
	class MediaSourceMonitor : public StatsMonitor<MediaSourceStats>
	{
	public:
		struct Metrics
		{
			std::optional<double> deltaFrames;
			std::optional<double> deltaTotalAudioEnergy;
			std::optional<double> deltaSamplesDuration;
			// Frames the source produced per second over the last interval.
			std::optional<double> producedFps;
			// Root mean square audio level over the last interval.
			std::optional<double> rmsAudioLevel;
		};

	public:
		MediaSourceMonitor(PeerConnectionMonitor* peerConnection, const MediaSourceStats& stats);

	public:
		void Accept(const MediaSourceStats& stats);
		const Metrics& GetMetrics() const
		{
			return this->metrics;
		}
		OutboundTrackMonitor* GetTrack() const;
		std::vector<OutboundRtpMonitor*> GetOutboundRtps() const;

	private:
		Metrics metrics;
	};

	/** One audio playout device (`media-playout`), possibly shared by several inbound tracks. */
	class MediaPlayoutMonitor : public StatsMonitor<MediaPlayoutStats>
	{
	public:
		struct Metrics
		{
			double deltaSynthesizedSamplesDuration{ 0 };
			double deltaSamplesDuration{ 0 };
			std::optional<double> deltaSynthesizedSamplesEvents;
			std::optional<double> deltaTotalPlayoutDelay;
			std::optional<double> deltaSamplesCount;
			std::optional<double> playoutDelayPerSampleInMs;
			std::optional<double> synthesizedSamplesRatio;
		};

	public:
		MediaPlayoutMonitor(PeerConnectionMonitor* peerConnection, const MediaPlayoutStats& stats);

	public:
		void Accept(const MediaPlayoutStats& stats);
		const Metrics& GetMetrics() const
		{
			return this->metrics;
		}
		Detectors& GetDetectors()
		{
			return this->detectors;
		}

	private:
		Metrics metrics;
		Detectors detectors;
	};

	/** One data channel (`data-channel`). */
	class DataChannelMonitor : public StatsMonitor<DataChannelStats>
	{
	public:
		struct Metrics
		{
			std::optional<double> deltaBytesSent;
			std::optional<double> deltaBytesReceived;
			std::optional<double> sendingBitrate;
			std::optional<double> receivingBitrate;
		};

	public:
		DataChannelMonitor(PeerConnectionMonitor* peerConnection, const DataChannelStats& stats);

	public:
		void Accept(const DataChannelStats& stats);
		const Metrics& GetMetrics() const
		{
			return this->metrics;
		}

	private:
		Metrics metrics;
	};

	/** The connection-level counters (`peer-connection`). */
	class PeerConnectionTransportMonitor : public StatsMonitor<PeerConnectionTransportStats>
	{
	public:
		PeerConnectionTransportMonitor(
		  PeerConnectionMonitor* peerConnection, const PeerConnectionTransportStats& stats);

	public:
		void Accept(const PeerConnectionTransportStats& stats);
	};

	/** One DTLS certificate (`certificate`). */
	class CertificateMonitor : public StatsMonitor<CertificateStats>
	{
	public:
		CertificateMonitor(PeerConnectionMonitor* peerConnection, const CertificateStats& stats);

	public:
		void Accept(const CertificateStats& stats);
	};
} // namespace observertc::clientmonitor

#endif
