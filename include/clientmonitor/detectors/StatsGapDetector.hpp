#ifndef CM_DETECTORS_STATS_GAP_DETECTOR_HPP
#define CM_DETECTORS_STATS_GAP_DETECTOR_HPP

#include "clientmonitor/detectors/Detector.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace observertc::clientmonitor
{
	// Fast forward declarations.
	class ClientMonitor;

	/**
	 * Telemetry: a collection that ran much later than scheduled (a stalled event loop, a
	 * suspended process, a blocked collector). Reported as OnStatsCollectionGap() and as a
	 * STATS_COLLECTION_GAP client event. Port of the JS `StatsGapDetector`.
	 */
	class StatsGapDetector : public Detector
	{
	public:
		explicit StatsGapDetector(ClientMonitor* clientMonitor);

	public:
		const std::string& GetName() const override;
		void Update() override;

	private:
		ClientMonitor* clientMonitor{ nullptr };
		std::optional<int64_t> previousCollectionStartedAt;
	};
} // namespace observertc::clientmonitor

#endif
