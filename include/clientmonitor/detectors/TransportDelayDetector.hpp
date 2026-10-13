#ifndef CM_DETECTORS_TRANSPORT_DELAY_DETECTOR_HPP
#define CM_DETECTORS_TRANSPORT_DELAY_DETECTOR_HPP

#include "clientmonitor/utils/SlicedWindow.hpp"
#include "clientmonitor/detectors/Detector.hpp"

#include <optional>
#include <string>

namespace observertc::clientmonitor
{
	// Fast forward declarations.
	class PeerConnectionMonitor;

	/**
	 * Transport quality: the round trip, averaged over the connection's detection window, at or
	 * above `thresholdInMs`. RTCP is preferred, ICE used when RTCP has no measurements. Resolves
	 * once the recovery window reads below `recoveryThresholdInMs`. Raises
	 * `transport-delay-degraded`. Port of the JS `TransportDelayDetector`.
	 */
	class TransportDelayDetector : public Detector
	{
	public:
		static constexpr const char* IssueType{ "transport-delay-degraded" };

		struct Reading
		{
			double rttInMs{ 0 };
			// "rtcp" or "ice".
			std::string source;
		};

	public:
		explicit TransportDelayDetector(PeerConnectionMonitor* peerConnection);

	public:
		const std::string& GetName() const override;
		void Update() override;
		/** True when the window was ready but carried no round trip measurement at all. */
		bool IsInputsUnavailable() const
		{
			return this->inputsUnavailable;
		}

	private:
		static std::optional<Reading> ReadRtt(const SlicedWindow::Slice& slice);
		void Raise(const Reading& reading, double windowInMs);
		void Resolve(const std::string& comment);

	private:
		PeerConnectionMonitor* peerConnection{ nullptr };
		std::string issueKey;
		bool raised{ false };
		bool inputsUnavailable{ false };
		std::optional<int64_t> startedAt;
	};
} // namespace observertc::clientmonitor

#endif
