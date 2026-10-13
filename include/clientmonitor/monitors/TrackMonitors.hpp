#ifndef CM_MONITORS_TRACK_MONITORS_HPP
#define CM_MONITORS_TRACK_MONITORS_HPP

#include "clientmonitor/utils/IssueRegistry.hpp"
#include "clientmonitor/utils/SlicedWindow.hpp"
#include "clientmonitor/MediaTrack.hpp"
#include "clientmonitor/detectors/Detector.hpp"
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
	class InboundRtpMonitor;
	class MediaSourceMonitor;
	class OutboundRtpMonitor;
	class PeerConnectionMonitor;

	/** A score with the reasons that took points off it. See the JS `CalculatedScore`. */
	struct CalculatedScore
	{
		double weight{ 1 };
		// nullopt means too few collections to judge yet, which is not the same as 0.
		std::optional<double> value;
		std::map<std::string, double> reasons;
	};

	/**
	 * What the application knows about an inbound track and the stats never reveal. Declared
	 * through ClientMonitor::SetInboundTrackContext(); a field left nullopt keeps its previous
	 * value when merged.
	 */
	struct InboundTrackContext
	{
		// "camera" or "screenshare".
		std::optional<std::string> contentType;
		// The video track this audio track belongs with (AV sync).
		std::optional<std::string> linkedVideoTrackId;
		// "lowmotion", "highmotion".
		std::optional<std::string> motionType;
		// How large the video is actually shown, in device pixels.
		std::optional<std::pair<int, int>> presentedResolution;
		std::optional<bool> paused;
		std::optional<bool> remoteOutboundTrackPaused;
		std::optional<bool> dtxMode;

		/** Fields set on `other` override; unset ones keep this value. */
		void MergeFrom(const InboundTrackContext& other);
	};

	struct OutboundTrackContext
	{
		std::optional<std::string> contentType;
		std::optional<bool> paused;

		void MergeFrom(const OutboundTrackContext& other);
	};

	/**
	 * One received media track, built over its inbound RTP stream. Port of the JS
	 * `InboundTrackMonitor`.
	 */
	class InboundTrackMonitor
	{
	public:
		/** The running totals fed into this track's sliced window, by index. */
		enum WindowTotal : size_t
		{
			TOTAL_FRAMES_RECEIVED = 0,
			TOTAL_FRAMES_DECODED,
			TOTAL_FRAMES_DROPPED,
			TOTAL_FRAMES_RENDERED,
			TOTAL_KEY_FRAMES_DECODED,
			TOTAL_PACKETS_RECEIVED,
			TOTAL_BYTES_RECEIVED,
			TOTAL_PLI_COUNT,
			TOTAL_FREEZE_COUNT,
			TOTAL_FREEZES_DURATION_IN_MS,
			TOTAL_PLAYOUT_SYNTHESIZED_DURATION_IN_MS,
			TOTAL_PLAYOUT_SAMPLES_DURATION_IN_MS,
			TOTAL_PLAYOUT_SYNTHESIS_EVENTS,
			TOTAL_PLAYOUT_DELAY_IN_MS,
			TOTAL_PLAYOUT_SAMPLES_COUNT,
			WINDOW_TOTALS_COUNT
		};

		static constexpr double CleanQpRatio{ 0.5 };
		static constexpr double CoarseQpRatio{ 0.8 };

		/** Values the track derives on every collection. */
		struct Metrics
		{
			// Signed audio-minus-video playout difference, for an audio track with a linked video.
			std::optional<double> linkedVideoPlayoutDiffInMs;
			// How much the presented resolution magnifies the decoded one.
			std::optional<double> displayMagnification;
			// How far the quantizer sits between clean (0) and coarse (1).
			std::optional<double> quantizationDegradation;
		};

		/** Verdicts the detectors write. Undefined until the detector could judge. */
		struct Verdicts
		{
			std::optional<bool> dry;
			std::optional<std::string> frameFlowState;
			std::optional<bool> degradedFrameSupply;
			std::optional<double> decodingDegradation;
			std::optional<double> synthesizedAudioRatio;
			std::optional<double> jitterBufferStressSeverity;
			std::optional<double> videoPlayoutSkew;
			std::optional<double> decodeBudgetUtilization;
			std::optional<double> concealedSamplesSeverity;
			std::optional<double> audioInterruptionSeverity;
			std::optional<bool> overloadedDecoder;
			std::optional<bool> stalledFrameAssembly;
			std::optional<bool> playoutDiscrepancy;
			std::optional<bool> stuckDecoder;
			std::optional<bool> failedVideoRecovery;
		};

	public:
		InboundTrackMonitor(
		  std::shared_ptr<MediaTrack> track,
		  InboundRtpMonitor* inboundRtp,
		  const nlohmann::json& attachments);

	public:
		const std::shared_ptr<MediaTrack>& GetTrack() const
		{
			return this->track;
		}
		std::string GetTrackId() const
		{
			return this->track->GetId();
		}
		std::string GetKind() const;
		MediaTrack::ReadyState GetReadyState() const
		{
			return this->track->GetReadyState();
		}
		PeerConnectionMonitor* GetPeerConnection() const;
		InboundRtpMonitor* GetInboundRtp() const
		{
			return this->inboundRtp;
		}
		/** The linked video track, if declared and still monitored. */
		InboundTrackMonitor* GetLinkedVideoTrack() const;

		const InboundTrackContext& GetContext() const
		{
			return this->context;
		}
		void SetContext(const InboundTrackContext& context);
		bool IsPaused() const
		{
			return this->context.paused.value_or(false);
		}
		void SetPaused(bool paused)
		{
			this->context.paused = paused;
		}
		bool IsRemoteOutboundTrackPaused() const
		{
			return this->context.remoteOutboundTrackPaused.value_or(false);
		}
		void SetRemoteOutboundTrackPaused(bool paused)
		{
			this->context.remoteOutboundTrackPaused = paused;
		}
		bool IsScreenShare() const
		{
			return this->context.contentType == "screenshare";
		}

		std::optional<double> GetBitrate() const;
		std::optional<double> GetJitter() const;
		std::optional<double> GetFractionLost() const;

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
		CalculatedScore& GetCalculatedScore()
		{
			return this->calculatedScore;
		}
		std::optional<double> GetScore() const
		{
			return this->calculatedScore.value;
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

		/** Called by the client monitor after every collection. */
		void Update();
		InboundTrackSample CreateSample() const;

	private:
		void RefreshQuantizationDegradation();
		void RefreshDisplayMagnification();
		void RefreshLinkedVideoPlayoutDiff();
		void FeedSlicedWindow();

	private:
		std::shared_ptr<MediaTrack> track;
		InboundRtpMonitor* inboundRtp{ nullptr };
		nlohmann::json attachments;
		nlohmann::json appData = nlohmann::json::object();
		InboundTrackContext context;
		Metrics metrics;
		Verdicts verdicts;
		CalculatedScore calculatedScore;
		IssueRegistry issues;
		std::unique_ptr<SlicedWindow> slicedWindow;
		Detectors detectors;
	};

	/**
	 * One sent media track, built over its media source and the outbound RTP streams encoding it
	 * (one per simulcast layer). Port of the JS `OutboundTrackMonitor`.
	 */
	class OutboundTrackMonitor
	{
	public:
		enum WindowTotal : size_t
		{
			MEDIA_SOURCE_TOTAL_PRODUCED_FRAMES = 0,
			HIGHEST_LAYER_TOTAL_ENCODED_FRAMES,
			WINDOW_TOTALS_COUNT
		};

		struct Metrics
		{
			// Summed over the layers.
			double bitrate{ 0 };
			double jitter{ 0 };
			double fractionLost{ 0 };
			double sendingPacketRate{ 0 };
			double remoteReceivedPacketRate{ 0 };
			// Whether the capture settings moved since the previous collection.
			std::optional<bool> videoCaptureSettingsChanged;
		};

		struct Verdicts
		{
			std::optional<bool> dry;
			std::optional<bool> degradedVideoCapture;
			std::optional<double> videoCaptureDegradation;
			std::optional<bool> degradedEncodingPerformance;
			std::optional<double> videoEncodingDegradation;
			std::optional<bool> silentAudioSource;
			std::optional<bool> loopbackAudioInput;
			std::optional<bool> lostCaptureSource;
		};

	public:
		OutboundTrackMonitor(
		  std::shared_ptr<MediaTrack> track,
		  MediaSourceMonitor* mediaSource,
		  const nlohmann::json& attachments);

	public:
		const std::shared_ptr<MediaTrack>& GetTrack() const
		{
			return this->track;
		}
		std::string GetTrackId() const
		{
			return this->track->GetId();
		}
		std::string GetKind() const
		{
			return this->track->GetKind();
		}
		MediaTrack::ReadyState GetReadyState() const
		{
			return this->track->GetReadyState();
		}
		PeerConnectionMonitor* GetPeerConnection() const;
		MediaSourceMonitor* GetMediaSource() const
		{
			return this->mediaSource;
		}
		/** Every outbound RTP stream (simulcast layer) encoding this track. */
		std::vector<OutboundRtpMonitor*> GetOutboundRtps() const;
		/** The layer carrying the most bits this collection: the one to judge an encoder on. */
		OutboundRtpMonitor* GetHighestLayer() const
		{
			return this->highestLayer;
		}

		const OutboundTrackContext& GetContext() const
		{
			return this->context;
		}
		void SetContext(const OutboundTrackContext& context);
		bool IsPaused() const
		{
			return this->context.paused.value_or(false);
		}
		void SetPaused(bool paused)
		{
			this->context.paused = paused;
		}
		/** The declared content type, else one inferred from the capture settings. */
		std::optional<std::string> GetContentType() const;
		bool IsScreenShare() const
		{
			return this->GetContentType() == "screenshare";
		}
		/** Set when the source ended by itself; the track is dropped after a last look. */
		bool IsSourceEnded() const
		{
			return this->sourceEnded;
		}
		void SetSourceEnded(bool ended)
		{
			this->sourceEnded = ended;
		}
		const std::optional<MediaTrack::Settings>& GetSettings() const
		{
			return this->settings;
		}

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
		CalculatedScore& GetCalculatedScore()
		{
			return this->calculatedScore;
		}
		std::optional<double> GetScore() const
		{
			return this->calculatedScore.value;
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

		/** Called by the client monitor after every collection. */
		void Update();
		OutboundTrackSample CreateSample() const;

	private:
		friend class PeerConnectionMonitor;

		void RefreshCaptureSettings();

	private:
		std::shared_ptr<MediaTrack> track;
		MediaSourceMonitor* mediaSource{ nullptr };
		nlohmann::json attachments;
		nlohmann::json appData = nlohmann::json::object();
		OutboundTrackContext context;
		std::optional<std::string> detectedContentType;
		std::optional<MediaTrack::Settings> settings;
		bool sourceEnded{ false };
		// The layers, keyed by ssrc. Maintained by the peer connection monitor.
		std::map<int64_t, OutboundRtpMonitor*> outboundRtps;
		OutboundRtpMonitor* highestLayer{ nullptr };
		double highestLayerTotalEncodedFrames{ 0 };
		Metrics metrics;
		Verdicts verdicts;
		CalculatedScore calculatedScore;
		IssueRegistry issues;
		std::unique_ptr<SlicedWindow> slicedWindow;
		Detectors detectors;
	};
} // namespace observertc::clientmonitor

#endif
