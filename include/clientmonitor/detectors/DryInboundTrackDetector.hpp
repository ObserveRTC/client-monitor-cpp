#ifndef CM_DETECTORS_DRY_INBOUND_TRACK_DETECTOR_HPP
#define CM_DETECTORS_DRY_INBOUND_TRACK_DETECTOR_HPP

#include "clientmonitor/detectors/Detector.hpp"

#include <optional>
#include <string>

namespace observertc::clientmonitor
{
	// Fast forward declarations.
	class InboundTrackMonitor;

	/**
	 * Pipeline disruption, receive chain: a live, unpaused inbound track whose RTP stream received
	 * no bytes for `thresholdInMs`. Raises `dry-inbound-track` on the track and reports
	 * OnDryInboundTrack(). Port of the JS `DryInboundTrackDetector`.
	 */
	class DryInboundTrackDetector : public Detector
	{
	public:
		static constexpr const char* IssueType{ "dry-inbound-track" };

	public:
		explicit DryInboundTrackDetector(InboundTrackMonitor* trackMonitor);

	public:
		const std::string& GetName() const override;
		void Update() override;

	private:
		void Raise(double duration);
		void Resolve(const std::string& comment);

	private:
		InboundTrackMonitor* trackMonitor{ nullptr };
		std::string issueKey;
		std::optional<int64_t> startedDryAt;
		double dryForInMs{ 0 };
	};
} // namespace observertc::clientmonitor

#endif
