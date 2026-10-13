#ifndef CM_DETECTORS_DRY_OUTBOUND_TRACK_DETECTOR_HPP
#define CM_DETECTORS_DRY_OUTBOUND_TRACK_DETECTOR_HPP

#include "clientmonitor/detectors/Detector.hpp"

#include <optional>
#include <string>

namespace observertc::clientmonitor
{
	// Fast forward declarations.
	class OutboundTrackMonitor;

	/**
	 * Pipeline disruption, send chain: a live, unmuted, unpaused outbound track whose active layers
	 * sent no bytes for `thresholdInMs`, with no bandwidth or CPU limitation to explain it. Raises
	 * `dry-outbound-track` on the track and reports OnDryOutboundTrack(). Port of the JS
	 * `DryOutboundTrackDetector`.
	 */
	class DryOutboundTrackDetector : public Detector
	{
	public:
		static constexpr const char* IssueType{ "dry-outbound-track" };

	public:
		explicit DryOutboundTrackDetector(OutboundTrackMonitor* trackMonitor);

	public:
		const std::string& GetName() const override;
		void Update() override;

	private:
		void Raise(double dryForInMs, size_t activeLayers);
		void Resolve(const std::string& comment);

	private:
		OutboundTrackMonitor* trackMonitor{ nullptr };
		std::string issueKey;
		std::optional<int64_t> startedDryAt;
		double dryForInMs{ 0 };
	};
} // namespace observertc::clientmonitor

#endif
