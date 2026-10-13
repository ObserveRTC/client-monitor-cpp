#define CM_CLASS "TransportDelayDetector"

#include "clientmonitor/detectors/TransportDelayDetector.hpp"
#include "clientmonitor/utils/Utils.hpp"
#include "clientmonitor/ClientMonitor.hpp"
#include "clientmonitor/Logger.hpp"
#include "clientmonitor/monitors/PeerConnectionMonitor.hpp"

namespace observertc::clientmonitor
{
	TransportDelayDetector::TransportDelayDetector(PeerConnectionMonitor* peerConnection)
	  : peerConnection(peerConnection),
	    issueKey(std::string(IssueType) + "-pc-" + peerConnection->GetPeerConnectionId())
	{
	}

	const std::string& TransportDelayDetector::GetName() const
	{
		static const std::string Name{ "transport-delay-detector" };

		return Name;
	}

	void TransportDelayDetector::Update()
	{
		if (this->disabled)
		{
			return;
		}

		const auto& config = this->peerConnection->GetParent()->GetConfig().transportDelayDetector;

		if (!config.has_value())
		{
			return;
		}

		const auto& window = this->peerConnection->GetSlicedWindow();

		if (!window.HasSlice("detection"))
		{
			return;
		}

		const auto& detectionWindow = window.GetSlice("detection");

		if (!detectionWindow.IsReady() || detectionWindow.GetDurationInMs() < 1)
		{
			return;
		}

		const auto detection = ReadRtt(detectionWindow);

		if (!detection.has_value())
		{
			this->inputsUnavailable = true;

			if (this->raised)
			{
				this->Resolve("round trip no longer measurable");
			}

			return;
		}

		this->inputsUnavailable = false;

		if (config->thresholdInMs <= detection->rttInMs)
		{
			return this->Raise(*detection, detectionWindow.GetDurationInMs());
		}
		if (!this->raised)
		{
			return;
		}

		// The stretch before the detection window must have recovered too, when it can be read.
		std::optional<Reading> recovery;

		if (window.HasSlice("recovery") && window.GetSlice("recovery").IsReady())
		{
			recovery = ReadRtt(window.GetSlice("recovery"));
		}

		const auto& clearing = recovery.has_value() ? *recovery : *detection;

		if (config->recoveryThresholdInMs <= clearing.rttInMs)
		{
			return;
		}

		this->Resolve("round trip recovered");
	}

	std::optional<TransportDelayDetector::Reading> TransportDelayDetector::ReadRtt(
	  const SlicedWindow::Slice& slice)
	{
		const auto mean = [](
		                    const std::optional<double>& timeInMs,
		                    const std::optional<double>& count,
		                    const char* source) -> std::optional<Reading>
		{
			if (!timeInMs.has_value() || !count.has_value() || *count <= 0)
			{
				return std::nullopt;
			}

			return Reading{ *timeInMs / *count, source };
		};

		auto rtcp = mean(
		  slice.GetDelta(PeerConnectionMonitor::TOTAL_RTCP_ROUND_TRIP_TIME_IN_MS),
		  slice.GetDelta(PeerConnectionMonitor::TOTAL_RTCP_ROUND_TRIP_MEASUREMENTS),
		  "rtcp");

		if (rtcp.has_value())
		{
			return rtcp;
		}

		return mean(
		  slice.GetDelta(PeerConnectionMonitor::TOTAL_ICE_ROUND_TRIP_TIME_IN_MS),
		  slice.GetDelta(PeerConnectionMonitor::TOTAL_ICE_RESPONSES_RECEIVED),
		  "ice");
	}

	void TransportDelayDetector::Raise(const Reading& reading, double windowInMs)
	{
		const nlohmann::json payload = {
			{ "peerConnectionId", this->peerConnection->GetPeerConnectionId() },
			{ "rttInMs",          reading.rttInMs                             },
			{ "rttSource",        reading.source                              },
			{ "sustainedForInMs", windowInMs                                  },
		};

		if (this->raised)
		{
			this->peerConnection->GetIssues().Update(
			  { this->issueKey, payload, std::nullopt, std::nullopt });

			return;
		}

		this->raised    = true;
		this->startedAt = utils::NowMs();

		auto* clientMonitor = this->peerConnection->GetParent();

		clientMonitor->Emit(
		  [&](ClientMonitor::Listener* listener)
		  {
			  listener->OnTransportDelayDegraded(clientMonitor, this->peerConnection, reading.rttInMs);
		  });

		IssueRegistry::RaiseInput input;

		input.key             = this->issueKey;
		input.type            = IssueType;
		input.payload         = payload;
		input.includeInSample = this->includeIssueInSample;

		this->peerConnection->GetIssues().Raise(input);
	}

	void TransportDelayDetector::Resolve(const std::string& comment)
	{
		this->raised = false;

		auto& issues = this->peerConnection->GetIssues();
		auto payload = nlohmann::json();

		if (const auto* issue = issues.Get(this->issueKey))
		{
			payload = issue->payload;

			if (this->startedAt.has_value())
			{
				payload["durationInMs"] = utils::NowMs() - *this->startedAt;
			}
		}

		issues.Resolve({ this->issueKey, comment, payload, utils::NowMs() });

		this->startedAt.reset();
	}
} // namespace observertc::clientmonitor
