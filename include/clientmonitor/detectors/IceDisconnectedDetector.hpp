#ifndef CM_DETECTORS_ICE_DISCONNECTED_DETECTOR_HPP
#define CM_DETECTORS_ICE_DISCONNECTED_DETECTOR_HPP

#include "clientmonitor/detectors/Detector.hpp"

#include <map>
#include <optional>
#include <string>

namespace clientmonitor
{
	// Fast forward declarations.
	class IceTransportMonitor;
	class PeerConnectionMonitor;

	/**
	 * Connectivity, layer 5: an ICE transport that stays `disconnected` for
	 * `disconnectedThresholdInMs` of stats time. One issue per transport and ICE generation; an
	 * ICE restart (a new local ufrag) resolves the old one. Raises `ice-disconnected`. Port of the
	 * JS `IceDisconnectedDetector`.
	 */
	class IceDisconnectedDetector : public Detector
	{
	public:
		static constexpr const char* IssueType{ "ice-disconnected" };

	public:
		explicit IceDisconnectedDetector(PeerConnectionMonitor* peerConnection);

	public:
		const std::string& GetName() const override;
		void Update() override;

	private:
		struct TransportState
		{
			int iceGeneration{ 0 };
			std::optional<std::string> usernameFragment;
			double disconnectedForInMs{ 0 };
			std::optional<int64_t> raisedAt;
		};

		void CheckTransport(IceTransportMonitor* transport);
		static std::optional<std::string> UsernameFragmentOf(IceTransportMonitor* transport);
		TransportState& GetState(IceTransportMonitor* transport);
		void Resolve(const std::string& transportId, const std::string& comment);
		std::string IssueKey(const std::string& transportId) const;

	private:
		PeerConnectionMonitor* peerConnection{ nullptr };
		std::map<std::string, TransportState> states;
	};
} // namespace clientmonitor

#endif
