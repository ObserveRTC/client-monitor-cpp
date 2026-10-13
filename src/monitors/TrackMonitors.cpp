#define CM_CLASS "TrackMonitors"

#include "clientmonitor/monitors/TrackMonitors.hpp"
#include "clientmonitor/utils/MediaMath.hpp"
#include "clientmonitor/utils/Utils.hpp"
#include "clientmonitor/ClientMonitor.hpp"
#include "clientmonitor/Logger.hpp"
#include "clientmonitor/detectors/DryInboundTrackDetector.hpp"
#include "clientmonitor/detectors/DryOutboundTrackDetector.hpp"
#include "clientmonitor/monitors/MediaMonitors.hpp"
#include "clientmonitor/monitors/PeerConnectionMonitor.hpp"
#include "clientmonitor/monitors/RtpMonitors.hpp"

#include <cmath>

namespace clientmonitor
{
	namespace
	{
		template<typename T>
		void MergeField(std::optional<T>& target, const std::optional<T>& source)
		{
			if (source.has_value())
			{
				target = source;
			}
		}

		std::optional<double> InMs(const std::optional<double>& seconds)
		{
			return seconds.has_value() ? std::optional<double>(*seconds * 1000) : std::nullopt;
		}
	} // namespace

	/* Contexts */

	void InboundTrackContext::MergeFrom(const InboundTrackContext& other)
	{
		MergeField(this->contentType, other.contentType);
		MergeField(this->linkedVideoTrackId, other.linkedVideoTrackId);
		MergeField(this->motionType, other.motionType);
		MergeField(this->presentedResolution, other.presentedResolution);
		MergeField(this->paused, other.paused);
		MergeField(this->remoteOutboundTrackPaused, other.remoteOutboundTrackPaused);
		MergeField(this->dtxMode, other.dtxMode);
	}

	void OutboundTrackContext::MergeFrom(const OutboundTrackContext& other)
	{
		MergeField(this->contentType, other.contentType);
		MergeField(this->paused, other.paused);
	}

	/* InboundTrackMonitor */

	InboundTrackMonitor::InboundTrackMonitor(
	  std::shared_ptr<MediaTrack> track, InboundRtpMonitor* inboundRtp, const nlohmann::json& attachments)
	  : track(std::move(track)),
	    inboundRtp(inboundRtp),
	    attachments(attachments),
	    issues(inboundRtp->GetPeerConnection()->GetParent()->GetActiveIssues().AsSink())
	{
		auto* clientMonitor = this->GetPeerConnection()->GetParent();
		const auto& config  = clientMonitor->GetConfig();

		this->slicedWindow = std::make_unique<SlicedWindow>(
		  clientMonitor->CreateWindowConfig(config.inboundTrackWindow, WINDOW_TOTALS_COUNT));

		if (config.dryInboundTrackDetector.has_value())
		{
			this->detectors.Add(std::make_unique<DryInboundTrackDetector>(this));
		}

		// The remaining inbound-track detectors of the JS library (AV desync, concealment,
		// decoder bottleneck, ...) register here as they are ported.

		// mediasoup's probator consumer is not media anyone watches.
		if (this->track->GetId() == "probator")
		{
			this->detectors.Clear();
		}
	}

	std::string InboundTrackMonitor::GetKind() const
	{
		return this->inboundRtp->GetKind();
	}

	PeerConnectionMonitor* InboundTrackMonitor::GetPeerConnection() const
	{
		return this->inboundRtp->GetPeerConnection();
	}

	InboundTrackMonitor* InboundTrackMonitor::GetLinkedVideoTrack() const
	{
		if (!this->context.linkedVideoTrackId.has_value())
		{
			return nullptr;
		}

		auto* linked =
		  this->GetPeerConnection()->GetInboundTrackMonitor(*this->context.linkedVideoTrackId);

		return linked && linked->GetKind() == "video" ? linked : nullptr;
	}

	void InboundTrackMonitor::SetContext(const InboundTrackContext& context)
	{
		this->context.MergeFrom(context);
		this->RefreshDisplayMagnification();
	}

	std::optional<double> InboundTrackMonitor::GetBitrate() const
	{
		return this->inboundRtp->GetMetrics().bitrate;
	}

	std::optional<double> InboundTrackMonitor::GetJitter() const
	{
		return this->inboundRtp->GetStats().jitter;
	}

	std::optional<double> InboundTrackMonitor::GetFractionLost() const
	{
		return this->inboundRtp->GetMetrics().deltaFractionLost;
	}

	void InboundTrackMonitor::Update()
	{
		this->RefreshDisplayMagnification();
		this->RefreshQuantizationDegradation();
		this->RefreshLinkedVideoPlayoutDiff();
		this->FeedSlicedWindow();
		this->detectors.Update();
	}

	InboundTrackSample InboundTrackMonitor::CreateSample() const
	{
		InboundTrackSample sample;

		sample.id           = this->track->GetId();
		sample.kind         = this->track->GetKind();
		sample.timestamp    = static_cast<double>(utils::NowMs());
		sample.score        = this->calculatedScore.value;
		sample.scoreReasons = utils::SampledScoreReasons(
		  this->calculatedScore.reasons,
		  this->GetPeerConnection()->GetParent()->GetConfig().sendScoreReasonsToServer);
		sample.attachments = this->attachments;

		return sample;
	}

	void InboundTrackMonitor::RefreshQuantizationDegradation()
	{
		const auto normalizedQp = this->inboundRtp->GetMetrics().normalizedQp;

		this->metrics.quantizationDegradation =
		  !normalizedQp.has_value() || CoarseQpRatio <= CleanQpRatio
		    ? std::nullopt
		    : std::optional<double>(
		        utils::Clamp((*normalizedQp - CleanQpRatio) / (CoarseQpRatio - CleanQpRatio), 0, 1));
	}

	void InboundTrackMonitor::RefreshDisplayMagnification()
	{
		const auto& presented    = this->context.presentedResolution;
		const auto decodedWidth  = this->inboundRtp->GetStats().frameWidth.value_or(0);
		const auto decodedHeight = this->inboundRtp->GetStats().frameHeight.value_or(0);

		this->metrics.displayMagnification =
		  presented.has_value() && 0 < presented->first && 0 < presented->second && decodedWidth &&
		      decodedHeight
		    ? std::optional<double>(std::sqrt(
		        (static_cast<double>(presented->first) * presented->second) /
		        (static_cast<double>(decodedWidth) * static_cast<double>(decodedHeight))))
		    : std::nullopt;
	}

	void InboundTrackMonitor::RefreshLinkedVideoPlayoutDiff()
	{
		this->metrics.linkedVideoPlayoutDiffInMs.reset();

		if (this->GetKind() != "audio")
		{
			return;
		}

		const auto audioPlayout = this->inboundRtp->GetStats().estimatedPlayoutTimestamp;
		auto* linked            = this->GetLinkedVideoTrack();
		const auto videoPlayout = linked ? linked->GetInboundRtp()->GetStats().estimatedPlayoutTimestamp
		                                 : std::optional<double>();

		if (!audioPlayout.has_value() || !videoPlayout.has_value())
		{
			return;
		}

		this->metrics.linkedVideoPlayoutDiffInMs = *audioPlayout - *videoPlayout;
	}

	void InboundTrackMonitor::FeedSlicedWindow()
	{
		const auto& s = this->inboundRtp->GetStats();
		auto* playout = this->inboundRtp->GetMediaPlayout();
		SlicedWindow::Values values(WINDOW_TOTALS_COUNT);

		values[TOTAL_FRAMES_RECEIVED]        = utils::ToDouble(s.framesReceived);
		values[TOTAL_FRAMES_DECODED]         = utils::ToDouble(s.framesDecoded);
		values[TOTAL_FRAMES_DROPPED]         = utils::ToDouble(s.framesDropped);
		values[TOTAL_FRAMES_RENDERED]        = utils::ToDouble(s.framesRendered);
		values[TOTAL_KEY_FRAMES_DECODED]     = utils::ToDouble(s.keyFramesDecoded);
		values[TOTAL_PACKETS_RECEIVED]       = utils::ToDouble(s.packetsReceived);
		values[TOTAL_BYTES_RECEIVED]         = utils::ToDouble(s.bytesReceived);
		values[TOTAL_PLI_COUNT]              = utils::ToDouble(s.pliCount);
		values[TOTAL_FREEZE_COUNT]           = utils::ToDouble(s.freezeCount);
		values[TOTAL_FREEZES_DURATION_IN_MS] = InMs(s.totalFreezesDuration);

		if (playout)
		{
			const auto& p = playout->GetStats();

			values[TOTAL_PLAYOUT_SYNTHESIZED_DURATION_IN_MS] = InMs(p.synthesizedSamplesDuration);
			values[TOTAL_PLAYOUT_SAMPLES_DURATION_IN_MS]     = InMs(p.totalSamplesDuration);
			values[TOTAL_PLAYOUT_SYNTHESIS_EVENTS] = utils::ToDouble(p.synthesizedSamplesEvents);
			values[TOTAL_PLAYOUT_DELAY_IN_MS]      = InMs(p.totalPlayoutDelay);
			values[TOTAL_PLAYOUT_SAMPLES_COUNT]    = utils::ToDouble(p.totalSamplesCount);
		}

		try
		{
			this->slicedWindow->Add(this->inboundRtp->GetStatsClockTime(), values);
		}
		catch (const std::exception& error)
		{
			CM_WARN(
			  "failed to feed the sliced window of track %s: %s", this->GetTrackId().c_str(), error.what());
		}
	}

	/* OutboundTrackMonitor */

	OutboundTrackMonitor::OutboundTrackMonitor(
	  std::shared_ptr<MediaTrack> track,
	  MediaSourceMonitor* mediaSource,
	  const nlohmann::json& attachments)
	  : track(std::move(track)),
	    mediaSource(mediaSource),
	    attachments(attachments),
	    issues(mediaSource->GetPeerConnection()->GetParent()->GetActiveIssues().AsSink())
	{
		auto* clientMonitor = this->GetPeerConnection()->GetParent();
		const auto& config  = clientMonitor->GetConfig();

		this->RefreshCaptureSettings();

		if (this->settings.has_value() && this->settings->displaySurface.has_value())
		{
			this->detectedContentType = "screenshare";
		}

		if (config.dryOutboundTrackDetector.has_value())
		{
			this->detectors.Add(std::make_unique<DryOutboundTrackDetector>(this));
		}

		// The remaining outbound-track detectors of the JS library (capture source lost, encoder
		// bottleneck, silent audio source, ...) register here as they are ported.

		this->slicedWindow = std::make_unique<SlicedWindow>(
		  clientMonitor->CreateWindowConfig(config.outboundTrackWindow, WINDOW_TOTALS_COUNT));
	}

	PeerConnectionMonitor* OutboundTrackMonitor::GetPeerConnection() const
	{
		return this->mediaSource->GetPeerConnection();
	}

	std::vector<OutboundRtpMonitor*> OutboundTrackMonitor::GetOutboundRtps() const
	{
		std::vector<OutboundRtpMonitor*> result;

		for (const auto& [ssrc, rtp] : this->outboundRtps)
		{
			result.push_back(rtp);
		}

		return result;
	}

	void OutboundTrackMonitor::SetContext(const OutboundTrackContext& context)
	{
		this->context.MergeFrom(context);
	}

	std::optional<std::string> OutboundTrackMonitor::GetContentType() const
	{
		return this->context.contentType.has_value() ? this->context.contentType
		                                             : this->detectedContentType;
	}

	void OutboundTrackMonitor::Update()
	{
		this->RefreshCaptureSettings();

		auto& m = this->metrics;

		m.bitrate                  = 0;
		m.jitter                   = 0;
		m.fractionLost             = 0;
		m.sendingPacketRate        = 0;
		m.remoteReceivedPacketRate = 0;
		this->highestLayer         = nullptr;

		for (const auto& [ssrc, rtp] : this->outboundRtps)
		{
			auto* remote = rtp->GetRemoteInboundRtp();

			m.bitrate += rtp->GetMetrics().bitrate.value_or(0);
			m.sendingPacketRate += rtp->GetMetrics().packetRate.value_or(0);

			if (remote)
			{
				m.jitter += remote->GetStats().jitter.value_or(0);
				m.fractionLost += remote->GetMetrics().deltaFractionLost.value_or(0);
				m.remoteReceivedPacketRate += remote->GetMetrics().packetRate.value_or(0);
			}

			if (
			  !this->highestLayer || rtp->GetMetrics().bitrate.value_or(0) >
			                           this->highestLayer->GetMetrics().bitrate.value_or(0))
			{
				this->highestLayer = rtp;
			}
		}

		if (this->highestLayer)
		{
			this->highestLayerTotalEncodedFrames +=
			  this->highestLayer->GetMetrics().deltaFramesEncoded.value_or(0);
		}

		SlicedWindow::Values values(WINDOW_TOTALS_COUNT);

		values[MEDIA_SOURCE_TOTAL_PRODUCED_FRAMES] =
		  utils::ToDouble(this->mediaSource->GetStats().frames);
		values[HIGHEST_LAYER_TOTAL_ENCODED_FRAMES] = this->highestLayerTotalEncodedFrames;

		try
		{
			this->slicedWindow->Add(this->mediaSource->GetStatsClockTime(), values);
		}
		catch (const std::exception& error)
		{
			CM_WARN(
			  "failed to feed the sliced window of track %s: %s", this->GetTrackId().c_str(), error.what());
		}

		this->detectors.Update();
	}

	OutboundTrackSample OutboundTrackMonitor::CreateSample() const
	{
		OutboundTrackSample sample;

		sample.id           = this->track->GetId();
		sample.kind         = this->track->GetKind();
		sample.timestamp    = static_cast<double>(utils::NowMs());
		sample.score        = this->calculatedScore.value;
		sample.scoreReasons = utils::SampledScoreReasons(
		  this->calculatedScore.reasons,
		  this->GetPeerConnection()->GetParent()->GetConfig().sendScoreReasonsToServer);
		sample.attachments = this->attachments;

		return sample;
	}

	void OutboundTrackMonitor::RefreshCaptureSettings()
	{
		const auto previous = this->settings;

		this->settings = this->track->GetSettings();

		if (!previous.has_value())
		{
			this->metrics.videoCaptureSettingsChanged.reset();

			return;
		}

		const auto& current = this->settings;

		this->metrics.videoCaptureSettingsChanged =
		  !current.has_value() || previous->frameRate != current->frameRate ||
		  previous->width != current->width || previous->height != current->height;
	}
} // namespace clientmonitor
