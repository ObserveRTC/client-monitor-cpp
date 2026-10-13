#define CM_CLASS "DryInboundTrackDetector"

#include "clientmonitor/detectors/DryInboundTrackDetector.hpp"
#include "clientmonitor/utils/Utils.hpp"
#include "clientmonitor/ClientMonitor.hpp"
#include "clientmonitor/Logger.hpp"
#include "clientmonitor/monitors/PeerConnectionMonitor.hpp"
#include "clientmonitor/monitors/TrackMonitors.hpp"

namespace clientmonitor
{
	DryInboundTrackDetector::DryInboundTrackDetector(InboundTrackMonitor* trackMonitor)
	  : trackMonitor(trackMonitor),
	    issueKey(std::string(IssueType) + "-track-" + trackMonitor->GetTrackId())
	{
	}

	const std::string& DryInboundTrackDetector::GetName() const
	{
		static const std::string Name{ "dry-inbound-track-detector" };

		return Name;
	}

	void DryInboundTrackDetector::Update()
	{
		auto& verdicts = this->trackMonitor->GetVerdicts();

		if (this->disabled)
		{
			verdicts.dry.reset();

			return;
		}

		auto* clientMonitor = this->trackMonitor->GetPeerConnection()->GetParent();
		const auto& config  = clientMonitor->GetConfig().dryInboundTrackDetector;

		if (!config.has_value())
		{
			return;
		}

		if (this->trackMonitor->GetReadyState() != MediaTrack::ReadyState::LIVE)
		{
			verdicts.dry.reset();
			this->dryForInMs = 0;

			if (this->startedDryAt.has_value())
			{
				this->Resolve("track ended");
			}

			return;
		}
		if (this->trackMonitor->IsPaused() || this->trackMonitor->IsRemoteOutboundTrackPaused())
		{
			verdicts.dry.reset();
			this->dryForInMs = 0;

			if (this->startedDryAt.has_value())
			{
				this->Resolve(this->trackMonitor->IsPaused() ? "consumer paused" : "remote track paused");
			}

			return;
		}

		auto* inboundRtp              = this->trackMonitor->GetInboundRtp();
		const auto deltaBytesReceived = inboundRtp->GetMetrics().deltaBytesReceived;

		if (!deltaBytesReceived.has_value())
		{
			verdicts.dry.reset();
		}
		if (deltaBytesReceived != 0.0)
		{
			if (deltaBytesReceived.has_value())
			{
				verdicts.dry = false;
			}

			this->dryForInMs = 0;

			if (this->startedDryAt.has_value())
			{
				this->Resolve("dry inbound track recovered");
			}

			return;
		}

		// Still dry while the issue is open; not yet dry while the stretch is being counted.
		verdicts.dry = this->startedDryAt.has_value();
		this->dryForInMs += inboundRtp->GetDeltaTime().value_or(0);

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
			  listener->OnDryInboundTrack(clientMonitor, this->trackMonitor);
		  });

		this->Raise(this->dryForInMs);
	}

	void DryInboundTrackDetector::Raise(double duration)
	{
		this->trackMonitor->GetVerdicts().dry = true;
		this->startedDryAt                    = utils::NowMs();

		IssueRegistry::RaiseInput input;

		input.key             = this->issueKey;
		input.type            = IssueType;
		input.includeInSample = this->includeIssueInSample;
		input.payload         = {
      { "trackId",  this->trackMonitor->GetTrackId() },
      { "duration", duration                         }
		};

		this->trackMonitor->GetIssues().Raise(input);
	}

	void DryInboundTrackDetector::Resolve(const std::string& comment)
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
} // namespace clientmonitor
