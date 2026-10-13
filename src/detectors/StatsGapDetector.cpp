#define CM_CLASS "StatsGapDetector"

#include "clientmonitor/detectors/StatsGapDetector.hpp"
#include "clientmonitor/ClientMonitor.hpp"
#include "clientmonitor/Logger.hpp"
#include "clientmonitor/schema/ClientEventTypes.hpp"

namespace observertc::clientmonitor
{
	StatsGapDetector::StatsGapDetector(ClientMonitor* clientMonitor) : clientMonitor(clientMonitor)
	{
	}

	const std::string& StatsGapDetector::GetName() const
	{
		static const std::string Name{ "stats-gap-detector" };

		return Name;
	}

	void StatsGapDetector::Update()
	{
		if (this->disabled)
		{
			return;
		}

		const auto& monitorConfig = this->clientMonitor->GetConfig();

		if (!monitorConfig.statsGapDetector.has_value())
		{
			return;
		}

		const auto& config   = *monitorConfig.statsGapDetector;
		const auto startedAt = this->clientMonitor->GetLastCollectingStatsAt();

		if (startedAt == 0)
		{
			return;
		}

		const auto previous = this->previousCollectionStartedAt;

		this->previousCollectionStartedAt = startedAt;

		if (!previous.has_value())
		{
			return;
		}

		const auto actualPeriodInMs = startedAt - *previous;
		const auto expectedPeriodInMs =
		  config.expectedPeriodInMs.value_or(monitorConfig.collectingPeriodInMs);

		// Without a configured period there is nothing to be late against.
		if (expectedPeriodInMs < 1)
		{
			return;
		}

		const bool overran = static_cast<double>(expectedPeriodInMs) * config.gapRatioThreshold <
		                       static_cast<double>(actualPeriodInMs) &&
		                     config.minGapInMs < actualPeriodInMs;

		if (!overran)
		{
			return;
		}

		ClientMonitor::Listener::StatsCollectionGapEvent event;

		event.expectedPeriodInMs = expectedPeriodInMs;
		event.actualPeriodInMs   = actualPeriodInMs;
		event.gapInMs            = actualPeriodInMs - expectedPeriodInMs;

		this->clientMonitor->Emit(
		  [&](ClientMonitor::Listener* listener)
		  {
			  listener->OnStatsCollectionGap(this->clientMonitor, event);
		  });

		if (!config.createEvent)
		{
			return;
		}

		this->clientMonitor->AddEvent(
		  ClientEventTypes::STATS_COLLECTION_GAP,
		  {
		    { "expectedPeriodInMs",            event.expectedPeriodInMs      },
		    { "actualPeriodInMs",              event.actualPeriodInMs        },
		    { "gapInMs",		                   event.gapInMs                 },
		    { "durationOfCollectingStatsInMs",
         this->clientMonitor->GetMetrics().durationOfCollectingStatsInMs },
    });
	}
} // namespace observertc::clientmonitor
