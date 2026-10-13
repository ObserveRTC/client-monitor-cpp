#define CM_CLASS "DryOutboundTrackDetector"

#include "clientmonitor/detectors/DryOutboundTrackDetector.hpp"
#include "clientmonitor/utils/Utils.hpp"
#include "clientmonitor/ClientMonitor.hpp"
#include "clientmonitor/Logger.hpp"
#include "clientmonitor/monitors/PeerConnectionMonitor.hpp"
#include "clientmonitor/monitors/TrackMonitors.hpp"

namespace observertc::clientmonitor
{
	DryOutboundTrackDetector::DryOutboundTrackDetector(OutboundTrackMonitor* trackMonitor)
	  : trackMonitor(trackMonitor),
	    issueKey(std::string(IssueType) + "-track-" + trackMonitor->GetTrackId())
	{
	}

	const std::string& DryOutboundTrackDetector::GetName() const
	{
		static const std::string Name{ "dry-outbound-track-detector" };

		return Name;
	}

	void DryOutboundTrackDetector::Update()
	{
		auto& verdicts = this->trackMonitor->GetVerdicts();

		if (this->disabled)
		{
			verdicts.dry.reset();

			return;
		}

		auto* clientMonitor = this->trackMonitor->GetPeerConnection()->GetParent();
		const auto& config  = clientMonitor->GetConfig().dryOutboundTrackDetector;

		if (!config.has_value())
		{
			return;
		}

		const auto& track = this->trackMonitor->GetTrack();

		if (this->trackMonitor->IsPaused() || track->IsMuted() || track->GetReadyState() != MediaTrack::ReadyState::LIVE)
		{
			verdicts.dry.reset();
			this->dryForInMs = 0;

			if (this->startedDryAt.has_value())
			{
				this->Resolve("track paused, muted or not live");
			}

			return;
		}

		std::vector<OutboundRtpMonitor*> activeRtps;

		for (auto* rtp : this->trackMonitor->GetOutboundRtps())
		{
			if (rtp->GetStats().active != false)
			{
				activeRtps.push_back(rtp);
			}
		}

		// An encoder that is limited is explained, not dry.
		std::optional<std::string> limitation;

		for (auto* rtp : activeRtps)
		{
			const auto& reason = rtp->GetStats().qualityLimitationReason;

			if (reason == "bandwidth" || reason == "cpu")
			{
				limitation = reason;
				break;
			}
		}

		if (limitation.has_value())
		{
			verdicts.dry.reset();
			this->dryForInMs = 0;

			if (this->startedDryAt.has_value())
			{
				this->Resolve("the encoder is limited by " + *limitation);
			}

			return;
		}
		if (activeRtps.empty())
		{
			verdicts.dry.reset();
			this->dryForInMs = 0;

			if (this->startedDryAt.has_value())
			{
				this->Resolve("no layer of this track is being sent");
			}

			return;
		}

		std::optional<double> deltaBytesSent;
		std::optional<double> deltaTime;

		for (auto* rtp : activeRtps)
		{
			if (rtp->GetMetrics().deltaBytesSent.has_value())
			{
				deltaBytesSent = deltaBytesSent.value_or(0) + *rtp->GetMetrics().deltaBytesSent;
			}
			if (!deltaTime.has_value())
			{
				deltaTime = rtp->GetDeltaTime();
			}
		}

		if (!deltaBytesSent.has_value())
		{
			verdicts.dry.reset();

			return;
		}
		if (*deltaBytesSent != 0)
		{
			verdicts.dry     = false;
			this->dryForInMs = 0;

			if (this->startedDryAt.has_value())
			{
				this->Resolve("dry outbound track recovered");
			}

			return;
		}

		// Still dry while the issue is open; not yet dry while the stretch is being counted.
		verdicts.dry = this->startedDryAt.has_value();
		this->dryForInMs += deltaTime.value_or(0);

		if (this->dryForInMs < static_cast<double>(config->thresholdInMs))
		{
			return;
		}
		if (this->startedDryAt.has_value())
		{
			return;
		}

		clientMonitor->Emit(
		  [&](ClientMonitor::Listener* listener)
		  {
			  listener->OnDryOutboundTrack(clientMonitor, this->trackMonitor);
		  });

		this->Raise(this->dryForInMs, activeRtps.size());
	}

	void DryOutboundTrackDetector::Raise(double dryForInMs, size_t activeLayers)
	{
		this->trackMonitor->GetVerdicts().dry = true;
		this->startedDryAt                    = utils::NowMs();

		IssueRegistry::RaiseInput input;

		input.key             = this->issueKey;
		input.type            = IssueType;
		input.includeInSample = this->includeIssueInSample;
		input.payload         = {
      { "trackId",      this->trackMonitor->GetTrackId() },
      { "dryForInMs",   dryForInMs                       },
      { "activeLayers", activeLayers                     }
		};

		this->trackMonitor->GetIssues().Raise(input);
	}

	void DryOutboundTrackDetector::Resolve(const std::string& comment)
	{
		auto& issues = this->trackMonitor->GetIssues();
		auto payload = nlohmann::json();

		if (const auto* issue = issues.Get(this->issueKey))
		{
			payload = issue->payload;

			if (this->startedDryAt.has_value())
			{
				payload["durationInMs"] = utils::NowMs() - *this->startedDryAt;
			}
		}

		issues.Resolve({ this->issueKey, comment, payload, utils::NowMs() });

		this->startedDryAt.reset();
	}
} // namespace observertc::clientmonitor
